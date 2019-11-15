/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>

#define N 1024
#define SIZE (N * sizeof(int))

int main(int argc, char *argv[]) {
	PMAT_CRASH_DISABLE();
	size_t mapped_len;
	int is_pmem;
	int check;

	/* create a pmem file and memory map it */
	int *arr;
	assert(posix_memalign((void **) &arr, PMAT_CACHELINE_SIZE, SIZE) == 0);
	PMAT_REGISTER("in-order-store.bin", arr, SIZE);

    // Initialize array sequentially...
	for (int i = 0; i < N; i++) {
		arr[i] = i;
		asm volatile ("clflush (%0)" :: "r"(arr + i));
        PMAT_FORCE_CRASH();
	}

	return 0;
}
