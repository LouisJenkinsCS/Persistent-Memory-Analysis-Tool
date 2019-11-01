#include "durable_queue.h"
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
	node->value = -1;
	node->deqThreadID = -1;
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

	dq->free_list = 0;
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

/*
    Assumption: Uninitialized portion of heap is zero'd.
*/
struct DurableQueue *DurableQueue_recovery(void *heap, size_t sz) PERSISTENT {
    // Header...
    struct DurableQueue *dq = (struct DurableQueue *) heap;
    // No setup has been done...
    if (dq->heap_base == NULL || dq->head == 0 || dq->tail == 0) {
        return DurableQueue_create(heap, sz);
    }
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
					DurableQueue_exit(dq);
                    return true;
                }
            } else {
				FLUSH(&last->next);
				atomic_compare_exchange_strong(&dq->tail, &last, (uintptr_t) next);
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
					}
				}
			}
		}
	}
}