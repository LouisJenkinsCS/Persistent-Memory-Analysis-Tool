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
#include <qsbr/gc.h>
#include <omp.h>

// Size is N + 1 as we need space for the sentinel node
#define N 1024
#define SIZE (sizeof(struct DurableQueue) + (N+1) * sizeof(struct DurableQueueNode))


int main(int argc, char *argv[]) {
	void *heap;
	assert(posix_memalign(&heap, PMAT_CACHELINE_SIZE, SIZE) == 0);
	PMAT_REGISTER("durable-queue.bin", heap, SIZE);
    struct DurableQueue *dq = DurableQueue_create(heap, SIZE);

    #pragma omp parallel 
	{
		DurableQueue_register(dq);
		for (int i = 0; i < N / omp_get_num_threads(); i++) {
			assert(DurableQueue_enqueue(dq, i) == true);
		}
		DurableQueue_unregister(dq);
	}
	assert(DurableQueue_enqueue(dq, -1) == false);
	free(heap);
	return 0;
}
