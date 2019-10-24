#include "durable_queue.h"
#include <assert.h>
#include <stddef.h>

// Allocate node; The node is made entirely persistent by the time this function returns...
struct DurableQueueNode *DurableQueueNode_create(void *heap, int value) PERSISTENT {
    struct DurableQueueNode *node = (struct DurableQueueNode *) heap;
    node->value = value;
    FLUSH(&node->value);
    node->deqThreadID = -1;
    FLUSH(&node->deqThreadID);
    node->next = 0;
    FLUSH(&node->next);
	node->alloc_list_next = NULL;
	node->free_list_next = NULL;
    return node;
}

struct DurableQueueNode *DurableQueue_alloc(struct DurableQueue *dq) TRANSIENT {
    struct DurableQueueNode *head;
	uintptr_t next;
	do {
		head = atomic_load(&dq->free_list);
		if (head == NULL) return NULL;
		next = head->free_list_next;
	} while(!atomic_compare_exchange_weak(&dq->free_list, &head, next));
	return head;
}

void DurableQueue_init(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
	uintptr_t head;
    do {
        head = atomic_load(&dq->alloc_list);
        node->alloc_list_next = head;
    } while(!atomic_compare_exchange_weak(&dq->alloc_list, &head, (uintptr_t) node));

    do {
        head = atomic_load(&dq->free_list);
        node->free_list_next = head;
    } while(!atomic_compare_exchange_weak(&dq->free_list, &head, (uintptr_t) node));
}

void DurableQueue_free(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT {
    uintptr_t head;
    do {
        head = dq->free_list;
        node->free_list_next = head;
    } while(!atomic_compare_exchange_weak(&dq->free_list, &head, (uintptr_t) node));
}

static void DurableQueue_gc(gc_entry_t *entry, void *arg) {
	DurableQueue_free(arg, (void *) ((uintptr_t) entry) - offsetof(struct DurableQueueNode, gc_next));
}

// THREAD MUST BE REGISTRERED!
static void DurableQueue_enter(struct DurableQueue *dq) {
	gc_crit_enter(dq->gc);
}

// THREAD MUST BE REGISTERED!
static void DurableQueue_exit(struct DurableQueue *dq) {
	gc_crit_exit(dq->gc);
}

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap, size_t sz) PERSISTENT {
    // Setup base of data structure...
    struct DurableQueue *dq = (struct DurableQueue *) heap;
    dq->heap_base = heap + sizeof(struct DurableQueue);
    FLUSH(&dq->heap_base);

	dq->free_list = NULL;
	dq->alloc_list = NULL;
	dq->gc = gc_create(offsetof(struct DurableQueueNode, gc_next), DurableQueue_free, dq);
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

void DurableQueue_register(struct DurableQueue *dq) TRANSIENT {
	gc_register(dq->gc);
}

void DurableQueue_unregister(struct DurableQueue *dq) TRANSIENT {
	gc_unregister(dq->gc);
}

/*
    Assumption: Uninitialized portion of heap is zero'd.
*/
struct DurableQueue *DurableQueue_recovery(void *heap, size_t sz) PERSISTENT {
    // Header...
    struct DurableQueue *dq = (struct DurableQueue *) heap;
    // No setup has been done...
    if (dq->heap_base == NULL || dq->head == NULL || dq->tail == NULL) {
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
        struct DurableQueueNode *last = atomic_load(&dq->tail);
        struct DurableQueueNode *next = atomic_load(&last->next);
        if (last == atomic_load(&dq->tail)) {
            if (next == NULL) {
                if (atomic_compare_exchange_strong(&last->next, (uintptr_t *) &next, (uintptr_t) node)) {
                    FLUSH(&last->next);
                    atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) &last, (uintptr_t) node);
					DurableQueue_exit(dq);
                    return true;
                }
            } else {
				FLUSH(&last->next);
				atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) last, (uintptr_t) next);
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
		struct DurableQueueNode *first = dq->head;
		struct DurableQueueNode *last = dq->tail;
		struct DurableQueueNode *next = first->next;
		if (first == dq->head) {
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
					atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) &last, next);
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
					atomic_compare_exchange_strong(&dq->head, &first, next);
					return retval;
				} else {
					// Help push their operation along...
					int *address = &dq->returnedValues[next->deqThreadID];
					if (dq->head == first) {
						// The owning thread has not yet pushed progress forward
						FLUSH(&next->deqThreadID);
						*address = retval;
						FLUSH(address);
						atomic_compare_exchange_strong(&dq->head, &first, next);
					}
				}
			}
		}
	}
}