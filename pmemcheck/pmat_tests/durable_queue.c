/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>
#include "durable_queue.h"
#include <gc.h>

#define N (64 * 1024)
#define SIZE (sizeof(struct DurableQueue) + N * sizeof(struct DurableQueueNode))


int main(int argc, char *argv[]) {
	void *heap;
	assert(posix_memalign(&heap, PMAT_CACHELINE_SIZE, SIZE) == 0);
	PMAT_REGISTER("durable-queue.bin", heap, SIZE);
    struct DurableQueue *dq = DurableQueue_create(heap, SIZE);

    #pragma omp parallel for
	for (int i = 0; i < N; i++) {
		DurableQueue_enqueue(dq, i);
	}
	return 0;
}
