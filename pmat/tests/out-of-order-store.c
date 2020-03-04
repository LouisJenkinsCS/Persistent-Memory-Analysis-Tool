/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include "utils.h"

#ifndef N
#define N (1024)
#endif
#define SIZE (N * sizeof(int))

int main(int argc, char *argv[]) {
	PMAT_CRASH_DISABLE();

	int *arr = CREATE_HEAP("out-of-order-store.bin", SIZE);
	assert (arr != (void *) -1);
	PMAT_REGISTER("out-of-order-store-shadow.bin", arr, SIZE);

    // Initialize array sequentially...
	for (int i = 0; i < N; i++) {
		arr[i] = i;
		CLFLUSHOPT(arr + i);
        PMAT_FORCE_CRASH();
	}

	return 0;
}
