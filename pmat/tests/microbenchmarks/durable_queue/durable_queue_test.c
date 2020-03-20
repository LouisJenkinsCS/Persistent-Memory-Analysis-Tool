/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include "durable_queue.h"
#include <omp.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

// Size is N + 1 as we need space for the sentinel node
#ifndef N
#define N (1024 * 1024)
#endif
#define SIZE (sizeof(struct DurableQueue) + (N+1) * sizeof(struct DurableQueueNode))

// Sanity Check to determine whether or not the queue is working...
static void check_queue(struct DurableQueue *dq) {
	long redux = 0;
	#pragma omp parallel for reduction (+:redux)
	for (int i = 0; i < N; i++) {
		assert(DurableQueue_enqueue(dq, i) == true);
		redux += i;
	}

	long checkredux = 0;
	for (struct DurableQueueNode *node = dq->head; node != NULL; node = node->next) {
		if (node == dq->head) {
			continue;
		}
		checkredux += node->value;
	}
	printf("Sanity check: redux = %lld, checkredux = %lld\n", redux, checkredux);

	printf("Finished enqueue with redux of %ld...\n", redux);
	// Ensure that the queue is filled to the brim and that we cannot allocate any more
	assert(DurableQueue_enqueue(dq, -1) == false); 

	long redux2 = 0;
	#pragma omp parallel for reduction (+:redux2) 
	for (int i = 0; i < N; i++) {
		long ret = DurableQueue_dequeue(dq, omp_get_thread_num());
		if (ret >= 0)
			redux2 += ret;
	}
	printf("Finished dequeue with redux of %ld...\n", redux2);

	// Sanity check: Should be empty
	assert(DurableQueue_dequeue(dq, omp_get_thread_num()) == DQ_EMPTY);
	assert(redux == redux2);
}

// Note: This performs randomized enqueue/dequeue operations in lock-step due to the stop-the-world approach
// to memory reclamation we use.
static void do_benchmark(struct DurableQueue *dq, int seconds) {
	srand(0);
	time_t start;
	time(&start);
	atomic_int status = 1; 
	size_t numOperations = 0;

	#pragma omp parallel reduction(+: numOperations)
	{
		#pragma omp master
		printf("Number of threads: %d\n", omp_get_num_threads());
		time_t end;

		while (true) {
			{
				time(&end);
				int time_taken = end - start;

				if (time_taken >= seconds) {
					break;
				}
			}
			
			numOperations++;
			int rng = rand();
			if (rng % 2 == 0) {
				bool success = DurableQueue_enqueue(dq, rng);
				if (!success) {
					DurableQueue_dequeue(dq, omp_get_thread_num());
				}
			} else {
				int retval = DurableQueue_dequeue(dq, omp_get_thread_num());
				if (retval == DQ_EMPTY) {
					bool success = DurableQueue_enqueue(dq, rng);

					if (!success) {
						assert(false && "Out of Memory!!!");
					}	
				}
			}
		}
		printf("Thread %d performed %lu operations\n", omp_get_thread_num(), numOperations);
	}
	printf("Performed %ld operations\n", numOperations);
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Need a single argument (seconds), but got %d...\n", argc - 1);
		exit(EXIT_FAILURE);
	}
	int seconds = atoi(argv[1]);
	if (seconds <= 0) {
		fprintf(stderr, "Received a time of %s seconds, but needs to be greater than 0!", argv[1]);
		exit(EXIT_FAILURE);
	}

	// Disable automatic crash simulation until after queue has been initialized
	// and sanity checking has been performed.
	PMAT_CRASH_DISABLE();
	void *heap;
	assert(posix_memalign(&heap, PMAT_CACHELINE_SIZE, SIZE) == 0);
	memset(heap, 0, SIZE);
	PMAT_REGISTER("durable-queue.bin", heap, SIZE);
    struct DurableQueue *dq = DurableQueue_create(heap, SIZE);
	#ifndef SKIP_SANITY_CHECK
	printf("Sanity checking queue...\n");
	check_queue(dq);
	printf("Sanity check complete, beginning benchmark for %d seconds...\n", seconds);
	#endif
	PMAT_CRASH_ENABLE();
	do_benchmark(dq, seconds);
	PMAT_CRASH_DISABLE();
    DurableQueue_destroy(dq);
	//PMAT_UNREGISTER_BY_ADDR(heap);
	free(heap);
	return 0;
}
