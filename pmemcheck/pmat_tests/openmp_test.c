/*
    Tests for three things...
	
	1)	PMAT is able to handle calls to third-party libraries, and handle compiler transformations
		(such as in the case of the `#omp parallel for` loop)
	2)	PMAT is able to handle parallelism relatively gracefully and expose potential bugs, despite
		Valgrind's inherent thread serialization.
	3)	PMAT is able to detect errors as program exit.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <assert.h>
#include <valgrind/pmemcheck.h>

#define N (64)
#define SIZE (N * sizeof(int))

int main(int argc, char *argv[]) {
	PMAT_CRASH_DISABLE();
	size_t mapped_len;
	int is_pmem;
	int check;

	/* create a pmem file and memory map it */
	int *arr;
	assert(posix_memalign((void **) &arr, PMAT_CACHELINE_SIZE, SIZE) == 0);
	PMAT_REGISTER("openmp_test.bin", arr, SIZE);

	// Parallel Identity-Map over 'persistent' memory...
	#pragma omp parallel for
	for (int i = 0; i < N; i++) {
		arr[i] = i;
		// Possible for another OpenMP thread to be working on some part of `i` while we
		// are doing this, hence making a race condition possible where a cache line
		// gets flushed before all values get written. I.E: the thread in charge of
		// i=6 may not have written `arr[6] = 6;` before its cache-line gets flushed,
		// which means it is possible for it to _never_ be written back to NVRAM. 
		if (i % 8 == 0) asm volatile("clflush %0" : "+m" (*(volatile char *)(arr + i)));
	} // An SFENCE is already injected in the OMP parallel loop

	PMAT_FORCE_CRASH();
	return 0;
}
