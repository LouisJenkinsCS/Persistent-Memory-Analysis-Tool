/*
    Test to determine whether or not we can catch errors where stores are
    written-back out-of-order due to a lack of an explicit fence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpmem.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define N 1024
#define SIZE (N * sizeof(int))

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
    int foundGap = 0;
    int foundCorruption = 0;
    int foundEnd = 0;
    for (int i = 1; i < N; i++) {
        if (arr[i] == 0) {
            foundEnd = 1;
        } else if (arr[i] != i) {
            foundCorruption = 1;
        } else {
            if (foundEnd) {
                foundGap = 1;
            }
        }
    }

    // Success!
    munmap(arr, sz);

    if (foundGap || foundCorruption) {
        fprintf(stderr, "Gap Found: %d, Corruption Found: %d\n", foundGap, foundCorruption);
        return PMAT_VERIFICATION_FAILURE;
    }
    return 0;
}
