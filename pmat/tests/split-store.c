/*
    Test to determine how PMAT handles stores that straddle cache-line boundaries.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include "utils.h"
#include <stdint.h>

#ifndef N
#define N (1024)
#endif
#define SIZE (N * sizeof(int))

int main(int argc, char *argv[]) {
	PMAT_CRASH_DISABLE();
	size_t mapped_len;
	int is_pmem;
	int check;

	/* create a pmem file and memory map it */
	int *arr = CREATE_HEAP("split-store.bin", SIZE);
	assert(arr != (void *) -1);
	PMAT_REGISTER("split-store-shadow.bin", arr, SIZE);

    // Initialize array sequentially...
	for (int i = 0; i < N - 1; i++) {
		*(int *) ((char *)(arr + i) + (sizeof(int) / 2)) = i + UINT16_MAX;
		CLFLUSH(((char *)(arr + i) + (sizeof(int) / 2)));
        PMAT_FORCE_CRASH();
	}

	return 0;
}
