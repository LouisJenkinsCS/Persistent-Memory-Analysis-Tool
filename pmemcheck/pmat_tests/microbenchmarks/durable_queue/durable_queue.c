#include "durable_queue.h"
#include <valgrind/pmemcheck.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// Allocate node; The node is made entirely persistent by the time this function returns...
struct DurableQueueNode *DurableQueueNode_create(void *heap, int value) PERSISTENT {
	struct DurableQueueNode *node = (struct DurableQueueNode *) heap;
	node->value = value;
	FLUSH(&node->value);
	node->deqThreadID = -1;
	FLUSH(&node->deqThreadID);
	node->next = 0;
	FLUSH(&node->next);
	PMAT_TRANSIENT(&node->alloc_list_next, sizeof(node->alloc_list_next));
	PMAT_TRANSIENT(&node->free_list_next, sizeof(node->free_list_next));
	node->alloc_list_next = 0;
	node->free_list_next = 0;
	return node;
}

struct DurableQueueNode *DurableQueue_alloc(struct DurableQueue *dq) TRANSIENT {
	struct DurableQueueNode *head;
	uintptr_t next;
	do {
		head = (void *) atomic_load(&dq->free_list);
		if (head == NULL) return NULL;
		next = head->free_list_next;
	} while(!atomic_compare_exchange_strong(&dq->free_list, &head, next));
	assert(head->free_list_next != (uintptr_t) head);
	return head;
}

static void push_alloc_list(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	uintptr_t head;
	do {
		head = atomic_load(&dq->alloc_list);
		node->alloc_list_next = head;
	} while(!atomic_compare_exchange_strong(&dq->alloc_list, &head, (uintptr_t) node));
}

void DurableQueue_init(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	uintptr_t head;
	do {
		head = atomic_load(&dq->alloc_list);
		node->alloc_list_next = head;
	} while(!atomic_compare_exchange_strong(&dq->alloc_list, &head, (uintptr_t) node));

	do {
		head = atomic_load(&dq->free_list);
		node->free_list_next = head;
	} while(!atomic_compare_exchange_strong(&dq->free_list, &head, (uintptr_t) node));
}

void DurableQueue_free(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	node->next = 0;
	FLUSH(&node->next);
	node->value = -1;
	FLUSH(&node->value);
	node->deqThreadID = -1;
	FLUSH(&	node->deqThreadID);
	node->free_list_next = 0;
	uintptr_t head;
	do {
		head = atomic_load(&dq->free_list);
		if (head == (uintptr_t) node) {
			printf("Next = 0x%lx, Free_List_Next = 0x%lx, Alloc_List_Next = 0x%lx, deqThreadID = %ld\n", node->next, node->free_list_next, node->alloc_list_next, node->deqThreadID);
			assert(0);
		}
		assert(head != (uintptr_t) node);
		node->free_list_next = head;
	} while(!atomic_compare_exchange_strong(&dq->free_list, &head, (uintptr_t) node));
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
			DurableQueue_free(dq, dq->heap_base + i * sizeof(struct DurableQueueNode));
		}
	}
	free(nodes);
}

static void DurableQueue_enter(struct DurableQueue *dq) {
	// NOP
}

static void DurableQueue_exit(struct DurableQueue *dq) {
	// NOP
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
	assert(node != NULL);
	dq->head = ATOMIC_VAR_INIT((uintptr_t) node);
	FLUSH(&dq->head);
	dq->tail = ATOMIC_VAR_INIT((uintptr_t) node);
	FLUSH(&dq->tail);
	for (int i = 0; i < MAX_THREADS; i++) {
		dq->returnedValues[i] = -1;
		FLUSH(dq->returnedValues + i);
	}
	return dq;
}

struct DurableQueue *DurableQueue_destroy(struct DurableQueue *dq) PERSISTENT {
	// NOP
}

void DurableQueue_register(struct DurableQueue *dq) TRANSIENT {
	// NOP
}

void DurableQueue_unregister(struct DurableQueue *dq) TRANSIENT {
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
		if (node->value == -1) {
			fprintf(stderr, "Reachable node has a value of -1!\n");
			return false;
		}
		actualNodesFound++;
	}

	if (!foundTail) {
		fprintf(stderr, "Did not find tail while traversing!\n");
		return false;
	}

	if (actualNodesFound < expectedNodesFound - MAX_THREADS) {
		fprintf(stderr, "Only found %ld nodes but expected to find at least %ld (%ld, %ld)!\n", actualNodesFound, expectedNodesFound, numEnqueue, numDequeue);
		return false;
	}
	return true;
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
		if (node->value == -1) {
			fprintf(stderr, "Reachable node has a value of -1!\n");
			return NULL;
		}
		actualNodesFound++;
	}

	if (actualNodesFound < expectedNodesFound) {
		fprintf(stderr, "Only found %ld nodes but expected to find at least %ld!\n", actualNodesFound, expectedNodesFound);
		return NULL;
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
	// Allocate node...
	struct DurableQueueNode *node = DurableQueue_alloc(dq);
	// Full... May need to dequeue for a bit...
	if (node == NULL) {
		return false;
	}

	DurableQueue_enter(dq);

	// Set and flush value to be written.
	node->value = value;
	FLUSH(&node->value);

	while (1) {
		struct DurableQueueNode *last = (void *) dq->tail;
		struct DurableQueueNode *next = (void *) last->next;
		if (last == (void *) dq->tail) {
			if (next == NULL) {
				if (atomic_compare_exchange_strong(&last->next, &next, (uintptr_t) node)) {
					FLUSH(&last->next);
					atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) node);
					FLUSH(&dq->tail);
					post_enqueue(dq);
					DurableQueue_exit(dq);
					return true;
				}
			} else {
				FLUSH(&last->next);
				atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) next);
				FLUSH(&dq->tail);
			}
		} 

#pragma omp taskyield
	}
}

int DurableQueue_dequeue(struct DurableQueue *dq, int_least64_t tid) PERSISTENT {
	// Reset returned values
	dq->returnedValues[tid] = DQ_NULL;
	FLUSH(&dq->returnedValues[tid]);
	DurableQueue_enter(dq);

	while (1) {
		struct DurableQueueNode *first = (void *) dq->head;
		struct DurableQueueNode *last = (void *) dq->tail;
		struct DurableQueueNode *next = (void *) first->next;
		if ((uintptr_t) first == dq->head) {
			// Is Empty
			if (first == last) {
				if (next == NULL) {
					dq->returnedValues[tid] = DQ_EMPTY;
					FLUSH(&dq->returnedValues[tid]);
					DurableQueue_exit(dq);
					return DQ_EMPTY;
				} else {
					// Outdated tail
					FLUSH(&last->next);
					atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) &last, (uintptr_t) next);
				}
			} else {
				int retval = next->value;
				int_least64_t expected_tid = -1;
				// Attempt to claim this queue node as our own
				if (atomic_compare_exchange_strong(&next->deqThreadID, &expected_tid, tid)) {
					// Paper does FLUSH(&first->next->deqThreadID) instead of just flushing &next->deqThreadID...
					FLUSH(&next->deqThreadID);
					dq->returnedValues[tid] = retval;
					FLUSH(&dq->returnedValues[tid]);
					atomic_compare_exchange_strong(&dq->head, (uintptr_t *) &first, (uintptr_t) next);
					FLUSH(&dq->head);
					post_dequeue(dq);
					DurableQueue_exit(dq);
					return retval;
				} else {
					// Help push their operation along...
					int *address = &dq->returnedValues[next->deqThreadID];
					if (dq->head == (uintptr_t) first) {
						// The owning thread has not yet pushed progress forward
						FLUSH(&next->deqThreadID);
						*address = retval;
						FLUSH(address);
						atomic_compare_exchange_strong(&dq->head, (uintptr_t *) &first, (uintptr_t) next);
						// Needed, despite not being mentioned in the paper.
						FLUSH(&dq->head);
					}
				}
			}
		}
	}
}
