/*
    Simple test to see if we can catch errors at end of program...
    
    Note: Only the OpenMP thread that has index `i` mod 8 will
    initiate a `clflush`, which means that there is a possiblity
    that indices that are _not_ modulo 8 _may_ have holes where
    their values are 0 due to another thread not writing not writing
    in time for the flush; verification tool should check for this pattern!
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmat.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef N
#define N (1024)
#endif
#define SIZE (N * sizeof(int))

// Expects to be called like `./verifier 1 filename.bin`
int main(int argc, char *argv[]) {
	assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    int fd = open(argv[2], O_RDONLY);
    assert(fd != -1);
    struct stat sb;
    int retval = fstat(fd, &sb);
    assert(retval != -1);
    size_t sz = sb.st_size;
    int *arr = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(arr != (void *) -1);

    // Verification...
    int bad = 0;
    for (int i = 0; i < N; i++) {
        if (arr[i] != i) {
            bad += 1;
        }
    }

    // Success!
    munmap(arr, sz);

    if (bad) {
        fprintf(stderr, "Array had %d bad values!\n", bad);
        return PMAT_VERIFICATION_FAILURE;
    }

    return EXIT_SUCCESS;
}
