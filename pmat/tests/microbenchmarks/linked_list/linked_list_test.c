#include "linked_list.h"
#ifndef DO_PMEMCHECK
#include <valgrind/pmat.h>
#else
// Need to add `-I` include
#include <valgrind/pmemcheck.h>
#include <libpmem.h>
#endif
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

	if (strchr("cgb", argv[1][0]) == NULL ||
			argv[1][1] != '\0') {
		printf("Usage: %s <c|g|b> N\n", argv[0]);
		exit(1);
	}

    size_t N = strtoul(argv[2], NULL, 10);
    assert(N > 0);
    size_t size = sizeof(struct list_root) + (N+1) * sizeof(struct list_node);
    
    #ifndef DO_PMEMCHECK
	void *heap = CREATE_HEAP("linked_list.bin", size);
	assert(heap != (void *) -1);
    #else
    size_t len;
    int is_pmem;
    void *heap = pmem_map_file(argv[3], 0, 0, 0, &len, &is_pmem);
    assert(len >= size);
    pmem_memset_persist(heap, 0, len);
    #endif
    #ifndef DO_PMEMCHECK
	PMAT_REGISTER("linked_list-shadow.bin", heap, size);
    #else
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(heap, size);
    #endif
    
	struct list_root *r = heap;

	char opt = argv[1][0];
	switch (opt) {
		case 'g': {
            for (int i = 1; i <= N; i++) {
			    list_insert_consistent(r, i, i);
            }
			break;
		}
		case 'b': {
            for (int i = 1; i <=  N; i++) {
			    list_insert_inconsistent(r, i, i);
            }
			break;
		}
		default: {
			printf("Unrecognized option: %c\n", opt);
			abort();
		}
	}
    
    #ifndef DO_PMEMCHECK
	PMAT_UNREGISTER_BY_ADDR(heap);
    #else
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(heap, size);
    #endif
	munmap(heap, size);
	return 0;
}
