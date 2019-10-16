#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#define MAX_THREADS 4

#define FLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr));
#define DQ_HEAP(dq) (dq->heap_base + dq->heap_offset)
#define DQ_HEAP_ALLOC(dq, sz) (dq->heap_base + atomic_fetch_add(&dq->heap_offset, sz))

struct Node {
    int value;
    int deqThreadID;
    atomic_uintptr_t next;
};

// Allocate node; Is made durable...
struct Node *Node_create(void *heap, int value) {
    struct Node *node = (struct Node *) heap;
    node->value = value;
    FLUSH(&node->value);
    node->deqThreadID = -1;
    FLUSH(&node->deqThreadID);
    node->next = 0;
    FLUSH(&node->next);
    return node;
}

struct DurableQueue {
    atomic_uintptr_t head;
    atomic_uintptr_t tail;
    int *returnedValues[MAX_THREADS];
    void *heap_base;
    atomic_uintptr_t heap_offset;
};

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap) {
    // Setup base of data structure...
    struct DurableQueue *dq = (struct DurableQueue *) heap;
    dq->heap_base = heap;
    FLUSH(&dq->heap_base);
    dq->heap_offset = sizeof(struct DurableQueue);
    FLUSH(&dq->heap_offset);
    struct Node *node = Node_create(DQ_HEAP_ALLOC(dq, sizeof(struct Node)), 0);// Already durable
    FLUSH(&dq->heap_offset);

    // Setup sentinel node
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

void DurableQueue_enqueue(struct DurableQueue *dq, int value) {
    // Allocate node...
    struct Node *node = Node_create(DQ_HEAP_ALLOC(dq, sizeof(struct Node)), value);
    FLUSH(&dq->heap_offset);

    while (1) {
        struct Node *last = (void *) dq->tail;
        struct Node *next = (void *) last->next;
        if (last == (void *) dq->tail) {
            if (next == NULL) {
                if (atomic_compare_exchange_strong(&last->next, (uintptr_t *) &next, (uintptr_t) node)) {
                    FLUSH(&last->next);
                    atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) &last, (uintptr_t) node);
                    return;
                }
            }
        } else {
            FLUSH(&last->next);
            atomic_compare_exchange_strong(&dq->tail, (uintptr_t *) last, (uintptr_t) next);
        }
    }
}