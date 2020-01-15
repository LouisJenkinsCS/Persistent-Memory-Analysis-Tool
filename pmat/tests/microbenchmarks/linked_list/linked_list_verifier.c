#include "linked_list.h"
#include <valgrind/pmat.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
    assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    int fd = open(argv[2], O_RDONLY);
    assert(fd != -1);
    struct stat sb;
    int retval = fstat(fd, &sb);
    assert(retval != -1);
    size_t sz = sb.st_size;
    void *heap = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(heap != (void *) -1);

    struct list_root *root = heap;
    if (check_consistency(root) == false) {
        return PMAT_VERIFICATION_FAILURE;
    } else {
	    return 0;
    }
}
