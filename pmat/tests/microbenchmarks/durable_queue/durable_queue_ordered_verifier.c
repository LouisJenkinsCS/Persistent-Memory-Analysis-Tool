/*
    Run recovery and test that its all working...
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

int main(int argc, char *argv[]) {
	assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    int fd = open(argv[2], O_RDONLY);
    assert(fd != -1);
    struct stat sb;
    int retval = fstat(fd, &sb);
    assert(retval != -1);
    size_t sz = sb.st_size;
    void *heap = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(heap != (void *) -1);
    //assert(SIZE == sz);
    struct DurableQueue *dq = DurableQueue_recovery(heap, sz);
    if (dq == NULL) return PMAT_VERIFICATION_FAILURE;
    // Ensure ordering is okay...
    int i = DurableQueue_dequeue(dq, 0);
    while (i != -1) {
        int j = DurableQueue_dequeue(dq, 0);
        assert(j == -1 || j == i + 1);
        i = j;
    }
    munmap(heap, sz);
    return 0;
}
