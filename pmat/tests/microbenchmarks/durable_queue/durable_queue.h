#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "config.h"

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
};

PERSISTENT struct DurableQueue {
    long padding;
    // metadata[0] = numEnqueues
    // metadata[1] = numDequeues
    // metadata[2-6] = padding... should be all 0's
    atomic_long metadata[8];
    atomic_uintptr_t head;
    atomic_uintptr_t tail;
    // Persistent pointer to a persistent integer
    int returnedValues[MAX_THREADS];
    void *heap_base;
    TRANSIENT atomic_uintptr_t free_list;
    TRANSIENT atomic_uintptr_t alloc_list;
};

// Allocate node; The node is made entirely persistent by the time this function returns...
struct DurableQueueNode *DurableQueueNode_create(void *heap, int value) PERSISTENT;

struct DurableQueueNode *DurableQueue_alloc(struct DurableQueue *dq) TRANSIENT;

void DurableQueue_free(struct DurableQueueNode *node, struct DurableQueue *dq) TRANSIENT;

void DurableQueue_init(struct DurableQueue *dq, struct DurableQueueNode *node) TRANSIENT;

// Currently, this data structure expects an _entire_ region of "persistent" memory
// as its heap, until a more suitable persistent memory allocator can be used...
struct DurableQueue *DurableQueue_create(void *heap, size_t sz) PERSISTENT;

struct DurableQueue *DurableQueue_destroy(struct DurableQueue *dq) PERSISTENT;

void DurableQueue_gc(struct DurableQueue *dq) TRANSIENT;

/*
    Assumption: Uninitialized portion of heap is zero'd.
*/
struct DurableQueue *DurableQueue_recovery(void *heap, size_t sz) PERSISTENT;

bool DurableQueue_verify(void *heap, size_t sz);

bool DurableQueue_enqueue(struct DurableQueue *dq, int value) PERSISTENT;

int DurableQueue_dequeue(struct DurableQueue *dq, int_least64_t tid) PERSISTENT;
