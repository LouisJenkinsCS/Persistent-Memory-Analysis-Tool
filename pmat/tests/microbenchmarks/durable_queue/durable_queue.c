#include "durable_queue.h"
#include <valgrind/pmat.h>
#include <assert.h>
#include <stddef.h>
#include <omp.h>
#include <string.h>
#include <stdio.h>
#include "hazard.h"
#include "../../utils.h"

// Allocate node; The node is made entirely persistent by the time this function returns...
struct DurableQueueNode *DurableQueueNode_create(void *heap, int value) PERSISTENT {
	struct DurableQueueNode *node = (struct DurableQueueNode *) heap;
	node->value = value;
	FLUSH(&node->value);
	node->deqThreadID = -1;
	FLUSH(&node->deqThreadID);
	node->next = 0;
	FLUSH(&node->next);
	node->seqNumber = 0;
	FLUSH(&node->seqNumber);
	PMAT_TRANSIENT(&node->alloc_list_next, sizeof(node->alloc_list_next));
	PMAT_TRANSIENT(&node->free_list_next, sizeof(node->free_list_next));
	node->alloc_list_next = 0;
	node->free_list_next = 0;
	return node;
}

struct DurableQueueNode *DurableQueue_alloc(struct DurableQueue *dq) TRANSIENT {
	struct DurableQueueNode *head;
	struct DurableQueueNode *next;
	do {
		head = (void *) atomic_load(&dq->free_list);
		if (head == NULL) return NULL;
		next = atomic_load(&head->free_list_next);
	} while(!atomic_compare_exchange_strong(&dq->free_list, &head, next));
	assert(head->free_list_next != (uintptr_t) head);
	atomic_store(&head->free_list_next, 0);
	return head;
}

static void push_alloc_list(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	struct DurableQueueNode *head;
	do {
		head = atomic_load(&dq->alloc_list);
		atomic_store(&node->alloc_list_next, head);
	} while(!atomic_compare_exchange_strong(&dq->alloc_list, &head, node));
}

void DurableQueue_init(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	uintptr_t head;
	do {
		head = atomic_load(&dq->alloc_list);
		node->alloc_list_next = head;
	} while(!__sync_bool_compare_and_swap((uintptr_t *) &dq->alloc_list, head, (uintptr_t) node));

	do {
		head = atomic_load(&dq->free_list);
		node->free_list_next = head;
	} while(!__sync_bool_compare_and_swap((uintptr_t *) &dq->free_list, head, (uintptr_t) node));
}

void DurableQueue_free(struct DurableQueueNode *node, struct DurableQueue *dq) TRANSIENT {
	// static atomic_uintptr_t last_node = 0;
	// static atomic_uintptr_t last_thread = -1;
	// int lasttid = atomic_load(&last_thread);
	// if (atomic_load(&last_node) == node) {
	// 	uintptr_t tmp = atomic_load(&last_node);
	// 	if (tmp == node) {
	// 		printf("CALLED TWICE!!!Current thread is %ld but last thread was %ld!\n", omp_get_thread_num(), lasttid);
	// 		assert(0);
	// 	}
	// }
	atomic_store(&node->free_list_next, 0);
	atomic_store(&node->next, 0);
	FLUSH(&node->next);
	node->value = -1;
	FLUSH(&node->value);
	atomic_store(&node->deqThreadID, -2);
	FLUSH(&	node->deqThreadID);
	struct DurableQueueNode *head = 0;
	do {
		head = atomic_load(&dq->free_list);
		if (head == node) {
			printf("!!!DOUBLE FREE!!!\nNext = 0x%lx, Free_List_Next = 0x%lx, Alloc_List_Next = 0x%lx, deqThreadID = %ld\n", node->next, node->free_list_next, node->alloc_list_next, node->deqThreadID);
			assert(0);
		}
		assert(head != (uintptr_t) node);
		atomic_store(&node->free_list_next, head);
	} while(!atomic_compare_exchange_strong(&dq->free_list, &head, (uintptr_t) node));
	// atomic_store(&last_node, node);
	// atomic_store(&last_thread, omp_get_thread_num());
}

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap, size_t sz) PERSISTENT {
	// Setup base of data structure...
	struct DurableQueue *dq = (struct DurableQueue *) heap;
	dq->heap_base = heap + sizeof(struct DurableQueue);
	FLUSH(&dq->heap_base);

	PMAT_TRANSIENT(&dq->free_list, sizeof(dq->free_list));
	dq->free_list = 0;
	PMAT_TRANSIENT(&dq->alloc_list, sizeof(dq->alloc_list));
	dq->alloc_list = 0;
	struct DurableQueueNode *nodes = dq->heap_base;
	// Allocate all nodes ahead of time and add them to the free list...
	for (size_t i = 0; i < (sz - sizeof(struct DurableQueue)) / sizeof(struct DurableQueueNode); i++) {
		DurableQueue_init(dq, DurableQueueNode_create(nodes + i, -1));
	}

	// Setup sentinel node
	struct DurableQueueNode *node = DurableQueue_alloc(dq);
	node->seqNumber = 1;
	FLUSH(&node->seqNumber);
	assert(node != NULL);
	dq->head = ATOMIC_VAR_INIT((uintptr_t) node);
	FLUSH(&dq->head);
	dq->tail = ATOMIC_VAR_INIT((uintptr_t) node);
	FLUSH(&dq->tail);
	for (int i = 0; i < MAX_THREADS; i++) {
		dq->returnedValues[i] = -1;
		FLUSH(dq->returnedValues + i);
	}
	hazard_register_destructor(DurableQueue_free, dq);
	return dq;
}

struct DurableQueue *DurableQueue_destroy(struct DurableQueue *dq) PERSISTENT {
	// NOP
}

// Verify - significantly faster than full on recovery...
bool DurableQueue_verify(void *heap, size_t sz) {
	// Header...
	struct DurableQueue *dq = (struct DurableQueue *) heap;
	intptr_t off;
	if (heap + sizeof(struct DurableQueue) > dq->heap_base) {
		off = (heap + sizeof(struct DurableQueue)) - dq->heap_base;
	} else {
		off = - (intptr_t) (dq->heap_base - (heap + sizeof(struct DurableQueue)));
	}
	dq->heap_base = heap + sizeof(*dq);
	dq->head += off;
	dq->tail += off;

	long numEnqueue = dq->metadata[0];
	long numDequeue = dq->metadata[1];
	long expectedNodesFound = numEnqueue - numDequeue;
	for (int i = 2; i < 8; i++) {
		if (dq->metadata[i] != 0) {
			fprintf(stderr, "Metadata (checksum) entry %d has value %ld and not 0!\n", i, dq->metadata[i]);
			return false;
		}
	}
	bool foundTail = false;
	long actualNodesFound = 0;
	for (struct DurableQueueNode *node = dq->head; node != NULL; node = node->next) {
		if (node->next) node->next += off;
		if (node == dq->tail) foundTail = true;
		if (node->value == -1 && node != dq->head) {
			fprintf(stderr, "Reachable node has a value of -1!\n");
			return false;
		}
		if (!node->next) atomic_store(&dq->tail, (uintptr_t) node);
		actualNodesFound++;
	}

	if (!foundTail) {
		fprintf(stderr, "Did not find tail while traversing!\n");
		return false;
	}

	if (actualNodesFound < expectedNodesFound - MAX_THREADS) {
		fprintf(stderr, "[Metadata] Only found %ld nodes but expected to find at least %ld (%ld, %ld)!\n", actualNodesFound, expectedNodesFound, numEnqueue, numDequeue);
		return false;
	}
	
	expectedNodesFound = 0;
	struct DurableQueueNode *head = dq->head;
	struct DurableQueueNode *tail = dq->tail;
	expectedNodesFound = tail->seqNumber - head->seqNumber + 1;
	if (actualNodesFound != expectedNodesFound) {
		fprintf(stderr, "[Sequence Number] Only found %ld nodes but expected to find at least %ld (%ld, %ld)!\n", actualNodesFound, expectedNodesFound, tail->seqNumber, head->seqNumber);
		return false;
	}
	return true;
}

// Must be called when _no other thread is mutating the queue_
// Mark and sweep (stop the world)
void DurableQueue_gc(struct DurableQueue *dq) TRANSIENT {
	struct DurableQueueNode *node = dq->head;

	// Calculate number of nodes...
	size_t sz = 0;
	for (struct DurableQueueNode *node = dq->alloc_list; node; node = node->alloc_list_next, sz++) ;

	// Allocate a buffer of nodes for mark-sweep
	struct DurableQueueNode **nodes = malloc(sizeof(struct DurableQueueNode *) * sz);
	for (int i = 0; i < sz; i++) nodes[i] = NULL;
	for (struct DurableQueueNode *node = dq->head; node; node = node->next) {
		nodes[(((uintptr_t) node) - (uintptr_t) dq->heap_base) / sizeof(struct DurableQueueNode)] = node;
	}

	// Anything that is NULL has not been found... add back to free list...
	for (size_t i = 0; i < sz; i++) {
		if(!nodes[i]) {
			DurableQueue_free(dq->heap_base + i * sizeof(struct DurableQueueNode), dq);
		}
	}
	free(nodes);
}

/*
   Recovery, but it should _only_ be called once we know for a fact that the
   heap has been initialized. Perhaps later we can extend recovery to test 
   to what extent the heap has been initialized and recover that way, but for
   the time being, this is merely a simple test.
   */
struct DurableQueue *DurableQueue_recovery(void *heap, size_t sz) PERSISTENT {
	// Header...
	struct DurableQueue *dq = (struct DurableQueue *) heap;
	intptr_t off;
	if (heap + sizeof(struct DurableQueue) > dq->heap_base) {
		off = (heap + sizeof(struct DurableQueue)) - dq->heap_base;
	} else {
		off = - (intptr_t) (dq->heap_base - (heap + sizeof(struct DurableQueue)));
	}
	dq->heap_base = heap + sizeof(*dq);

	// Reconstruct alloc_list
	struct DurableQueueNode *nodes = dq->heap_base;
	// Allocate all nodes ahead of time and add them to the free list...
	for (size_t i = 0; i < (sz - sizeof(struct DurableQueue)) / sizeof(struct DurableQueueNode); i++) {
		push_alloc_list(dq, nodes + i);
	}

	// Repair queue by redirecting pointers
	dq->head += off;
	dq->tail += off;
	for (struct DurableQueueNode *node = dq->head; node != NULL; node = node->next) {
		if (node->next) node->next += off;
		if (!node->next) atomic_store(&dq->tail, (uintptr_t) node);
	}

	// Reclaim
	DurableQueue_gc(dq);

	// Check if every reachable node is valid.
	long numEnqueue = dq->metadata[0];
	long numDequeue = dq->metadata[1];
	long expectedNodesFound = numEnqueue - numDequeue;
	for (int i = 2; i < 8; i++) {
		if (dq->metadata[i] != 0) {
			fprintf(stderr, "Metadata (checksum) entry %d has value %ld and not 0!\n", i, dq->metadata[i]);
			return NULL;
		}
	}
	long actualNodesFound = 0;
	for (struct DurableQueueNode *node = dq->head; node != NULL; node = node->next) {
		if (node->value == -1 && node != dq->head) {
			fprintf(stderr, "Reachable node has a value of -1!\n");
			return NULL;
		}
		actualNodesFound++;
	}

	if (actualNodesFound < expectedNodesFound - MAX_THREADS) {
		fprintf(stderr, "[Metadata] Only found %ld nodes but expected to find at least %ld (%ld, %ld)!\n", actualNodesFound, expectedNodesFound, numEnqueue, numDequeue);
		return false;
	}

	expectedNodesFound = 0;
	struct DurableQueueNode *head = dq->head;
	struct DurableQueueNode *tail = dq->tail;
	expectedNodesFound = tail->seqNumber - head->seqNumber + 1;
	if (actualNodesFound != expectedNodesFound) {
		fprintf(stderr, "[Sequence Number] Only found %ld nodes but expected to find at least %ld (%ld, %ld)!\n", actualNodesFound, expectedNodesFound, tail->seqNumber, head->seqNumber);
		return false;
	}
	return dq;
}

static void post_enqueue(struct DurableQueue *dq) {
	dq->metadata[0]++;
	FLUSH(&dq->metadata[0]);
}

static void post_dequeue(struct DurableQueue *dq) {
	dq->metadata[1]++;
	FLUSH(&dq->metadata[1]);
}

bool DurableQueue_enqueue(struct DurableQueue *dq, int value) PERSISTENT {
	assert(value != -1);
	// Allocate node...
	struct DurableQueueNode *node = DurableQueue_alloc(dq);
	// Full... May need to dequeue for a bit...
	if (node == NULL) {
		return false;
	}

	// Set and flush value to be written.
	node->value = value;	
	#if defined(DURABLE_QUEUE_BUG_FLUSHOPT) && DURABLE_QUEUE_BUG_FLUSHOPT & (1 << 0)
	CLFLUSHOPT(&node->value); // Not a bug!
	#elif defined(DURABLE_QUEUE_BUG) && DURABLE_QUEUE_BUG & (1 << 0)
	// NOP
	#else
	FLUSH(&node->value);
	#endif

	while (1) {
		struct DurableQueueNode *last = (void *) atomic_load(&dq->tail);
		hazard_acquire(0, last);
		if (last != (void *) atomic_load(&dq->tail)) {
			continue;
		}
		struct DurableQueueNode *next = (void *) atomic_load(&last->next);
		if (last == (void *) atomic_load(&dq->tail)) {
			if (next == NULL) {
				node->seqNumber = last->seqNumber + 1;
				FLUSH(&node->seqNumber);
				if (atomic_compare_exchange_strong(&last->next, &next, (uintptr_t) node)) {
					#if defined(DURABLE_QUEUE_BUG_FLUSHOPT) && DURABLE_QUEUE_BUG_FLUSHOPT & (1 << 1)
					CLFLUSHOPT(&last->next);
					#elif defined(DURABLE_QUEUE_BUG) && DURABLE_QUEUE_BUG & (1 << 1)
					// NOP
					#else
					FLUSH(&last->next);
					#endif
					atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) node);
					post_enqueue(dq);
					hazard_release(last, false);
					return true;
				}
			} else {
				#if defined(DURABLE_QUEUE_BUG_FLUSHOPT) && DURABLE_QUEUE_BUG_FLUSHOPT & (1 << 2)
				CLFLUSHOPT(&last->next);
				#elif defined(DURABLE_QUEUE_BUG) && DURABLE_QUEUE_BUG & (1 << 2)
				// NOP
				#else
				FLUSH(&last->next);
				#endif
				atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) next);
			}
		} 
	}
}

int DurableQueue_dequeue(struct DurableQueue *dq, int_least64_t tid) PERSISTENT {
	// Reset returned values
	dq->returnedValues[tid] = DQ_NULL;
	FLUSH(&dq->returnedValues[tid]);

	while (1) {
		struct DurableQueueNode *first = (void *) atomic_load(&dq->head);
		hazard_acquire(0, first);
		struct DurableQueueNode *last = (void *) atomic_load(&dq->tail);
		if (first != (void *) atomic_load(&dq->head)) {
			continue;
		}

		struct DurableQueueNode *next = (void *) atomic_load(&first->next);
		hazard_acquire(1, next);
		if ((uintptr_t) first == atomic_load(&dq->head)) {
			// Is Empty
			if (first == last) {
				if (next == NULL) {
					dq->returnedValues[tid] = DQ_EMPTY;
					FLUSH(&dq->returnedValues[tid]);
					hazard_release(first, false);
					hazard_release(next, false);
					return DQ_EMPTY;
				} else {
					#if defined(DURABLE_QUEUE_BUG_FLUSHOPT) && DURABLE_QUEUE_BUG_FLUSHOPT & (1 << 3)
					CLFLUSHOPT(&last->next);
					#elif defined(DURABLE_QUEUE_BUG) && DURABLE_QUEUE_BUG & (1 << 3)
					// NOP
					#else
					FLUSH(&last->next);
					#endif
					atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) next);
				}
			} else {
				int retval = next->value;
				assert(retval != -1);
				assert(next->free_list_next == 0);
				assert(tid != -1);
				int_least64_t expected_tid = -1;
				if (atomic_compare_exchange_strong(&dq->head, &first, next)){
					#if defined(DURABLE_QUEUE_BUG_FLUSHOPT) && DURABLE_QUEUE_BUG_FLUSHOPT & (1 << 4)
					CLFLUSHOPT(&dq->head);
					#elif defined(DURABLE_QUEUE_BUG) && DURABLE_QUEUE_BUG & (1 << 4)
					// NOP
=					#else
					FLUSH(&dq->head);
					#endif
					hazard_release(first, true);
					post_dequeue(dq);
					return retval;
				}
			}
		}
	}
}
