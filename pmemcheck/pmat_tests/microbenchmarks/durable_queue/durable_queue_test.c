/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>
#include "durable_queue.h"
#include <omp.h>
#include <time.h>

// Size is N + 1 as we need space for the sentinel node
#define N (1024 * 1024)
#define SIZE (sizeof(struct DurableQueue) + (N+1) * sizeof(struct DurableQueueNode))

// Sanity Check to determine whether or not the queue is working...
static void check_queue(struct DurableQueue *dq) {
	#pragma omp parallel
	{
		DurableQueue_register(dq);
		for (int i = 0; i < N / omp_get_num_threads(); i++) {
			assert(DurableQueue_enqueue(dq, i) == true);
		}
		#pragma omp barrier

		#pragma omp master
		printf("Finished enqueue...\n");

		// Ensure that the queue is filled to the brim and that we cannot allocate any more
		#pragma omp master
		assert(DurableQueue_enqueue(dq, -1) == false); 
		#pragma omp barrier

		for (int i = 0; i < N / omp_get_num_threads(); i++) {
			assert(DurableQueue_dequeue(dq, omp_get_thread_num()) >= 0);
		}
		#pragma omp barrier

		#pragma omp master
		printf("Finished dequeue...\n");

		// Sanity check: Should be empty
		assert(DurableQueue_dequeue(dq, omp_get_thread_num()) == DQ_EMPTY);

		#pragma omp master
		DurableQueue_gc(dq);


		DurableQueue_unregister(dq);
	}
	//DurableQueue_gc(dq);
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
		uint64_t iterations = 0;
		DurableQueue_register(dq);

		while (status != -1) {
			#pragma master
			{
				time(&end);
				int time_taken = end - start;

				if (time_taken >= seconds) {
					status = -1;
					continue;
				}
			}

			#pragma omp barrier
			if (status == 0) {
				#pragma barrier
				#pragma master
				{
					DurableQueue_gc(dq);
					status = 1;
				} 
				#pragma omp barrier // Hit next barrier so all threads leave barrier
			} else if (status == -1) {
				break;
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
						status = -1;
					}	
				}
			}
		}
		printf("Thread %d performed %lu operations\n", omp_get_thread_num(), numOperations);

		DurableQueue_unregister(dq);
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
	PMAT_REGISTER("durable-queue.bin", heap, SIZE);
    struct DurableQueue *dq = DurableQueue_create(heap, SIZE);
	printf("Sanity checking queue...\n");
	check_queue(dq);
	printf("Sanity check complete, beginning benchmark for %d seconds...\n", seconds);
	PMAT_CRASH_ENABLE();
	do_benchmark(dq, seconds);
	PMAT_CRASH_DISABLE();
    DurableQueue_destroy(dq);
	PMAT_UNREGISTER_BY_ADDR(heap);
	free(heap);
	return 0;
}
