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

int main(int argc, char *argv[]) {
	assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    int sz;
    int *arr = OPEN_HEAP(argv[2], O_RDONLY, &sz);
    assert(arr != (void *) -1);
    size_t numElems = sz / sizeof(int);
    printf("%d\n", sz);

    // Verification...
    int foundGap = 0;
    int foundCorruption = 0;
    int foundEnd = 0;
    for (int i = 1; i < numElems - 1; i++) {
        if (*(int *) ((char *)(arr + i) + (sizeof(int) / 2)) == 0) {
            foundEnd = 1;
        } else if (*(int *) ((char *)(arr + i) + (sizeof(int) / 2)) != i) {
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
