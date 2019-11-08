#include "linked_list.h"
#include <valgrind/pmemcheck.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
	void *heap;

	if (argc != 3 || strchr("cgb", argv[1][0]) == NULL ||
			argv[1][1] != '\0') {
		printf("Usage: %s <c|g|b> N\n", argv[0]);
		exit(1);
	}

    size_t N = strtoul(argv[2], NULL, 10);
    assert(N > 0);
    size_t size = sizeof(struct list_root) + (N+1) * sizeof(struct list_node);

	assert(posix_memalign(&heap, PMAT_CACHELINE_SIZE, size) == 0);
    memset(heap, 0, size);
	PMAT_REGISTER("linked_list.bin", heap, size);
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

	PMAT_UNREGISTER_BY_ADDR(heap);
	free(heap);
	return 0;
}
