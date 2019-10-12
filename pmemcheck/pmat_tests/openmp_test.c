/*
    Simple test to see if we can catch errors at end of program...
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <valgrind/pmemcheck.h>

#define N (64)
#define SIZE (N * sizeof(int))

int main(int argc, char *argv[]) {
	size_t mapped_len;
	int is_pmem;
	int check;

	/* create a pmem file and memory map it */
	int *arr = malloc(SIZE);
	VALGRIND_PMC_REGISTER("dummy.bin", arr, SIZE);

	// Parallel Zero-Initialize over 'persistent' memory...
	#pragma omp parallel for
	for (int i = 0; i < N; i++) {
		arr[i] = i;
		if (i % 8 == 0) asm volatile("clflush %0" : "+m" (*(volatile char *)(arr + i)));
	}

	VALGRIND_PMC_FORCE_CRASH();
	asm volatile("sfence" : : : "memory");

	return 0;
}
