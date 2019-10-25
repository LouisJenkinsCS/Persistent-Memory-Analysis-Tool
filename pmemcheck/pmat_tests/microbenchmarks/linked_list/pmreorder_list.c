/*
 * Copyright 2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pmreorder_list.c -- explains how to use pmreorder tool with libpmem
 *
 * usage: pmreorder_list <g|b|c> <path>
 * g - good case - add element to the list in a consistent way
 * b - bad case - add element to the list in an inconsistent way
 * c - check persistent consistency of the list
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>

#define FLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr));
#define MAX_NODES (1024 * 1024)
#define SIZE (sizeof(struct list_root))
#define NODE_PTR(root, node_id) \
	(node_id == 0 ? NULL : &(root)->nodes[(node_id)])

typedef size_t node_id;

struct list_node {
	int value;
	node_id next;
};

struct list_root {
	node_id head;
	struct list_node nodes[MAX_NODES + 1];
};

/*
 * check_consistency -- check if list meets the set requirements
 *
 * for consistent cases function returns 0
 * for inconsistent cases function returns 1
 */
static int
check_consistency(struct list_root *root)
{
	struct list_node *node = NODE_PTR(root, root->head);

	/*
	 * If node is linked to the list then its
	 * value should be set properly.
	 */
	if (node == NULL)
		return 0;
    int prev = -1;
    int i = 0;
	do {
		if (node->value == 0) {
			//printf("Came across bad node value %d\n", node->value);
            return -i;
        } 
        if (prev != -1 && prev != node->value + 1) {
            return prev - node->value;
        } else {
            prev = node->value;
        }
		node = NODE_PTR(root, node->next);
        i++;
	} while (node != NULL);

	return 0;
}

/*
 * list_print -- print all elements to the screen
 */
static void
list_print(struct list_root *list)
{
	FILE *fp;
	fp = fopen("pmreorder_list.log", "w+");
	fprintf(fp, "List:\n");

	struct list_node *node = NODE_PTR(list, list->head);
	if (node == NULL) {
		fprintf(fp, "List is empty");
		goto end;
	}

	do {
		fprintf(fp, "Value: %d\n", node->value);
		node = NODE_PTR(list, node->next);
	} while (node != NULL);

end:
	fclose(fp);
}

/*
 * list_insert_consistent -- add new element to the list in a consistent way
 */
static void
list_insert_consistent(struct list_root *root, node_id node, int value)
{
	struct list_node *new = NODE_PTR(root, node);

	new->value = value;
	FLUSH(&new->value);
	new->next = root->head;
	FLUSH(&new->next);

	root->head = node;
	FLUSH(&root->head);
}

/*
 * list_insert_inconsistent -- add new element to the list
 * in an inconsistent way
 */
static void
list_insert_inconsistent(struct list_root *root, node_id node, int value)
{
	struct list_node *new = NODE_PTR(root, node);

	new->next = root->head;
	FLUSH(&new->next);

	root->head = node;
	FLUSH(&root->head);

	new->value = value;
	FLUSH(&new->value);
}

int
main(int argc, char *argv[])
{
	void *heap;

	if (argc != 2 || strchr("cgb", argv[1][0]) == NULL ||
			argv[1][1] != '\0') {
		printf("Usage: pmreorder_list <c|g|b>\n");
		exit(1);
	}

	assert(posix_memalign(&heap, PMAT_CACHELINE_SIZE, SIZE) == 0);
	PMAT_REGISTER("linked_list.bin", heap, 32 * 1024 * 1024);
	struct list_root *r = heap;

	char opt = argv[1][0];
	switch (opt) {
		case 'g': {
            for (int i = 1; i <= MAX_NODES; i++) {
			    list_insert_consistent(r, i, i);
            }
			break;
		}
		case 'b': {
            for (int i = 1; i <=  MAX_NODES; i++) {
			    list_insert_inconsistent(r, i, i);
            }
			break;
		}
		case 'c': {
			int check = check_consistency(r);
			return check;
		}
		default: {
			printf("Unrecognized option: %c\n", opt);
			abort();
		}
	}

	//list_print(r);

	free(heap);
	return 0;
}
