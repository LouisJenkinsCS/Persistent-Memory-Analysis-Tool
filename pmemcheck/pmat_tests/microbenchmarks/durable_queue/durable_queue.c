#include "durable_queue.h"
#include <assert.h>

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

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap, size_t sz) PERSISTENT {
    // Setup base of data structure...
    struct DurableQueue *dq = (struct DurableQueue *) heap;
    dq->heap_base = heap + sizeof(struct DurableQueue);
    FLUSH(&dq->heap_base);

	dq->free_list = NULL;
	dq->alloc_list = NULL;
	dq->gc = NULL;
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
        dq->returnedValues[i] = NULL;
        FLUSH(dq->returnedValues + i);
    }
    return dq;
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