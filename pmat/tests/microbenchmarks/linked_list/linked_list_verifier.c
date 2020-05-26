#include "linked_list.h"
#include <valgrind/pmat.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../utils.h"

/*
 * - Modified from pmreorder_list.c (in PMDK/src/examples/pmreorder/)
 * 
 * linked_list_test.c 
 *
 * usage: linked_list <g|b|c> N
 * g - good case - add element to the list in a consistent way
 * b - bad case - add element to the list in an inconsistent way
 * N - Number of nodes (size of linked list is directly porportional to this value...)
 */
int main(int argc, char *argv[]) {
    #ifndef DO_PMEMCHECK
    assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);
    #else
    #endif

    int sz;
    void *heap = OPEN_HEAP(argv[2], O_RDONLY, &sz);
    assert(heap != (void *) -1);

    struct list_root *root = heap;
    if (check_consistency(root) == false) {
        return PMAT_VERIFICATION_FAILURE;
    } else {
	    return 0;
    }
}
