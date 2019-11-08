
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/pmemcheck.h>
#include <assert.h>
#include "linked_list.h"

/*
 * check_consistency -- check if list meets the set requirements
 *
 * for consistent cases function returns 0
 * for inconsistent cases function returns 1
 */
bool check_consistency(struct list_root *root) {
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
void list_print(struct list_root *list)
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
void list_insert_consistent(struct list_root *root, node_id node, int value)
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
void list_insert_inconsistent(struct list_root *root, node_id node, int value)
{
	struct list_node *new = NODE_PTR(root, node);

	new->next = root->head;
	FLUSH(&new->next);

	root->head = node;
	FLUSH(&root->head);

	new->value = value;
	FLUSH(&new->value);
}