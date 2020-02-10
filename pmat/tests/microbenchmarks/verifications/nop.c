#include <valgrind/pmat.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

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

	time_t start, end;
	time(&start);
	
	int *arr;
	assert(posix_memalign((void **) &arr, PMAT_CACHELINE_SIZE, 1024) == 0);
	memset(arr, 0, 1024);
	PMAT_REGISTER("dummy.bin", arr, 1024);	

	while (1) {
		time(&end);
		int t = end - start;
		if (t >= seconds) {
			break;
		}	
		PMAT_FORCE_CRASH();
	}
}
