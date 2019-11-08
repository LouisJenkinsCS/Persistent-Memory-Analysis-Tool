#pragma once

#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>

#define FLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr));
#define NODE_PTR(root, node_id) ((node_id) == 0 ? NULL : &(root)->nodes[(node_id)])

typedef size_t node_id;

struct list_node {
	long value;
	node_id next;
};

struct list_root {
	node_id head;
	struct list_node nodes[];
};

/*
 * check_consistency -- check if list meets the set requirements
 *
 * for consistent cases function returns 0
 * for inconsistent cases function returns 1
 */
bool check_consistency(struct list_root *root);

/*
 * list_print -- print all elements to the screen
 */
void list_print(struct list_root *list);

/*
 * list_insert_consistent -- add new element to the list in a consistent way
 */
void list_insert_consistent(struct list_root *root, node_id node, int value);

/*
 * list_insert_inconsistent -- add new element to the list
 * in an inconsistent way
 */
void list_insert_inconsistent(struct list_root *root, node_id node, int value);
