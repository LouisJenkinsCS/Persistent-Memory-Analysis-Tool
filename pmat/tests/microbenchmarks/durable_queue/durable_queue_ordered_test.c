#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include "durable_queue.h"
#include <time.h>

// Size is N + 1 as we need space for the sentinel node
#ifndef N
#define N (1024)
#endif
#define SIZE (sizeof(struct DurableQueue) + (N+1) * sizeof(struct DurableQueueNode))

static void do_benchmark(struct DurableQueue *dq, int seconds) {
    time_t start;
    time_t end;
	time(&start);
    while (1) {
        for (int i = 0; i < N; i++) {
            DurableQueue_enqueue(dq,i);
            time(&end);
            int time_taken = end - start;

            if (time_taken >= seconds) {
                return;
            }
            
        }
        for (int i = 0; i < N; i++) {
            assert(DurableQueue_dequeue(dq, 0) == i);
            time(&end);
            int time_taken = end - start;

            if (time_taken >= seconds) {
                return;
            }    
        }
    }
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
	PMAT_CRASH_ENABLE();
	do_benchmark(dq, seconds);
	PMAT_CRASH_DISABLE();
    DurableQueue_destroy(dq);
	free(heap);
	return 0;
}
