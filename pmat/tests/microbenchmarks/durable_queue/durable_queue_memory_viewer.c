/*
    Runs recovery and displays debug information about binary heap
*/

#include "durable_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define IN_BOUNDS(heap, sz, addr) \
    (((uintptr_t) heap) < ((uintptr_t) addr) && ((uintptr_t) heap + sz) > ((uintptr_t) addr))

#define _VERIFY_PTR(heap, base, sz, addr) \
    IN_BOUNDS(heap, sz, addr) ? (void *) addr : IN_BOUNDS(base, sz, addr) ? (void *) heap + ((uintptr_t) addr - (uintptr_t) base) : NULL;

#define VERIFY_PTR(heap, base, sz, addr) \
    ({ \
        void *ret = _VERIFY_PTR(heap, base, sz, addr); \
        assert(ret == NULL || IN_BOUNDS(heap, sz, ret) || IN_BOUNDS(base, sz, ret)); \
        ret; \
    })

char *stringify_node(struct DurableQueueNode *node) {
    char *buf = malloc(1024);
    snprintf(buf, 1024, "Node @ 0x%lX {\n"
    "\tvalue: %d\n"
    "\tdeqThreadId (Unused): %ld\n"
    "\tnext: 0x%lX\n"
    "\tseqNumber: %d\n"
    "\tfree_list_next: 0x%lX\n"
    "\talloc_list_next: 0x%lX\n"
    "}"
    , (uintptr_t) node, node->value, node->deqThreadID, node->next, node->seqNumber, node->free_list_next, node->alloc_list_next);
    return buf;
}

void dump_on_failure(void *heap, size_t sz) {
    struct DurableQueue *dq = heap;
    void *base = dq->heap_base;

    // The first two slots of the metadata contain the
    // number of enqueues and dequeues respectively. The
    // remaining six should be 0.
    printf("Metadata {");
    for (int i = 0; i < 8; i++) {
        switch (i) {
            case 0:
                printf("\n\tmetadata[0] (numEnqueues): %ld", dq->metadata[i]);
                break;
            case 1:
                printf("\n\tmetadata[1] (numDequeues): %ld", dq->metadata[i]);
                break;
            default:
                printf("\n\tmetadata[%d]: %ld%s", i, dq->metadata[i], dq->metadata[i] != 0 ? "(CORRUPTED)" : "");
        }
    }
    printf("\n}\n");

    // Check the head  and tail of the queue
    struct DurableQueueNode *head = VERIFY_PTR(heap, base, sz, dq->head);
    struct DurableQueueNode *tail = VERIFY_PTR(heap, base, sz, dq->tail);
    printf("head: %s\n", head ? stringify_node(head) : "(CORRUPTED)");
    printf("tail: %s\n", tail ? stringify_node(tail): "(CORRUPTED)");
}

int main(int argc, char *argv[]) {
	assert(argc >= 2);

    int fd = open(argv[1], O_RDONLY);
    assert(fd != -1);
    struct stat sb;
    int retval = fstat(fd, &sb);
    assert(retval != -1);
    size_t sz = sb.st_size;
    void *heap = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(heap != (void *) -1);
    //assert(SIZE == sz);

    printf("Recovery...\n");
    struct DurableQueue *dq = DurableQueue_recovery(heap, sz);
    if (dq == NULL) {
        printf("Recovery failed...\n");
        dump_on_failure(heap, sz);
    }
    munmap(heap, sz);
    return 0;
}
