#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <qsbr/gc.h>


#define MAX_THREADS 4

// For documentation purposes...
// Refers to something being transient or persistent
#define TRANSIENT
#define PERSISTENT

#define DQ_NULL -1
#define DQ_EMPTY -2

#define FLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr));
#define DQ_HEAP(dq) (dq->heap_base + dq->heap_offset)
#define DQ_HEAP_ALLOC(dq, sz) (dq->heap_base + atomic_fetch_add(&dq->heap_offset, sz))

PERSISTENT struct DurableQueueNode {
    int value;
    atomic_int_least64_t deqThreadID;
    atomic_uintptr_t next;
    TRANSIENT atomic_uintptr_t free_list_next;
    TRANSIENT atomic_uintptr_t alloc_list_next;
    // When we are in recovery, we just make this value 'NULL' if not already...
    TRANSIENT gc_entry_t PERSISTENT *gc_next;
};

// Lock-Free Treiber Stack
TRANSIENT struct FreeListNode {
    void *addr;
    atomic_uintptr_t next;
};


PERSISTENT struct DurableQueue {
    atomic_uintptr_t head;
    atomic_uintptr_t tail;
    // Persistent pointer to a persistent integer
    int PERSISTENT *returnedValues[MAX_THREADS];
    void *heap_base;
    TRANSIENT gc_t *gc; // Garbage collector
    TRANSIENT atomic_uintptr_t free_list;
    TRANSIENT atomic_uintptr_t alloc_list;
};

// Allocate node; The node is made entirely persistent by the time this function returns...
struct DurableQueueNode *DurableQueueNode_create(void *heap, int value) PERSISTENT;

struct DurableQueueNode *DurableQueue_alloc(struct DurableQueue *dq) TRANSIENT;

void DurableQueue_free(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT;

void DurableQueue_init(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT;

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap, size_t sz) PERSISTENT;

/*
    Assumption: Uninitialized portion of heap is zero'd.
*/
struct DurableQueue *DurableQueue_recovery(void *heap, size_t sz) PERSISTENT;

bool DurableQueue_enqueue(struct DurableQueue *dq, int value) PERSISTENT;

int DurableQueue_dequeue(struct DurableQueue *dq, int_least64_t tid) PERSISTENT;

void DurableQueue_register(struct DurableQueue *dq) TRANSIENT;

void DurableQueue_unregister(struct DurableQueue *dq) TRANSIENT;