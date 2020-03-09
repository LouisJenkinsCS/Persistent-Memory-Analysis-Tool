/*
 * Persistent memory checker.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, or (at your option) any later version, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <sys/param.h>
#include "pub_tool_oset.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pmat_include.h"

Word cmp_pmat_write_buffer_entries(const void *key, const void *elem) {
    const struct pmat_writeback_buffer_entry *lhs = (const struct pmat_writeback_buffer_entry *) (key);
    const struct pmat_writeback_buffer_entry *rhs = (const struct pmat_writeback_buffer_entry *) (elem);

    return cmp_pmat_cache_entries(lhs->entry, rhs->entry);
}

Word cmp_pmat_transient_entries(const void *key, const void *elem) {
    // LHS _must_ be the true 
    const struct pmat_transient_entry *lhs = (const struct pmat_transient_entry *) (key);
    const struct pmat_transient_entry *rhs = (const struct pmat_transient_entry *) (elem);
    
    if (lhs->addr + lhs->size <= rhs->addr)
        return -1;
    else if (lhs->addr >= rhs->addr + rhs->size)
        return 1;
    else
        return 0;
}

Word 
cmp_pmat_registered_files1(const void *key, const void *elem)
{
    const struct pmat_registered_file *lhs = (const struct pmat_registered_file *) (key);    
    const struct pmat_registered_file *rhs = (const struct pmat_registered_file *) (elem);
    
    if (lhs->addr + lhs->size <= rhs->addr)
        return -1;
    else if (lhs->addr >= rhs->addr + rhs->size)
        return 1;
    else
        return 0;
}

Word cmp_pmat_cache_entries(const void *key, const void *elem) {
    const struct pmat_cache_entry *lhs = (const struct pmat_cache_entry *) (key);
    const struct pmat_cache_entry *rhs = (const struct pmat_cache_entry *) (elem);
    
    return lhs->addr - rhs->addr;
}

static struct pmat_lru_node* create_lru_node(Addr key, void *value) 
{ 
    struct pmat_lru_node* node = VG_(malloc)("lru.node", sizeof(struct pmat_lru_node)); 
    node->key = key; 
    node->value = value;
    node->left = NULL;
    node->right = NULL;
    node->num_left = 0;
    node->num_right = 0; 
    return node; 
} 
  
// A utility function to right rotate subtree rooted with y 
// See the diagram given above. 
struct pmat_lru_node *rightRotate(struct pmat_lru_node *x) 
{ 
    struct pmat_lru_node *y = x->left; 
    x->left = y->right; 
    y->right = x; 
    x->num_left = (x->left) ? (x->left->num_left + x->left->num_right + 1) : 0;
    x->num_right = (x->right) ? (x->right->num_left + x->right->num_right + 1) : 0;
    y->num_left = (y->left) ? (y->left->num_left + y->left->num_right + 1) : 0;
    y->num_right = (y->right) ? (y->right->num_left + y->right->num_right + 1) : 0; 
    return y; 
} 
  
// A utility function to left rotate subtree rooted with x 
// See the diagram given above. 
struct pmat_lru_node *leftRotate(struct pmat_lru_node *x) 
{ 
    struct pmat_lru_node *y = x->right; 
    x->right = y->left; 
    y->left = x; 
    x->num_left = (x->left) ? (x->left->num_left + x->left->num_right + 1) : 0;
    x->num_right = (x->right) ? (x->right->num_left + x->right->num_right + 1) : 0;
    y->num_left = (y->left) ? (y->left->num_left + y->left->num_right + 1) : 0;
    y->num_right = (y->right) ? (y->right->num_left + y->right->num_right + 1) : 0; 
    return y; 
} 
  
// This function brings the key at root if key is present in tree. 
// If key is not present, then it brings the last accessed item at 
// root.  This function modifies the tree and returns the new root 
struct pmat_lru_node *splay(struct pmat_lru_node *root, Addr key) 
{ 
    // Base cases: root is NULL or key is present at root 
    if (root == NULL || root->key == key) 
        return root; 
  
    // Key lies in left subtree 
    if (root->key > key) 
    { 
        // Key is not in tree, we are done 
        if (root->left == NULL) return root; 
  
        // Zig-Zig (Left Left) 
        if (root->left->key > key) 
        { 
            // First recursively bring the key as root of left-left 
            root->left->left = splay(root->left->left, key); 
  
            // Do first rotation for root, second rotation is done after else 
            root = rightRotate(root); 
        } 
        else if (root->left->key < key) // Zig-Zag (Left Right) 
        { 
            // First recursively bring the key as root of left-right 
            root->left->right = splay(root->left->right, key); 
  
            // Do first rotation for root->left 
            if (root->left->right != NULL) 
                root->left = leftRotate(root->left); 
        } 
  
        // Do second rotation for root 
        return (root->left == NULL)? root: rightRotate(root); 
    } 
    else // Key lies in right subtree 
    { 
        // Key is not in tree, we are done 
        if (root->right == NULL) return root; 
  
        // Zag-Zig (Right Left) 
        if (root->right->key > key) 
        { 
            // Bring the key as root of right-left 
            root->right->left = splay(root->right->left, key); 
  
            // Do first rotation for root->right 
            if (root->right->left != NULL) 
                root->right = rightRotate(root->right); 
        } 
        else if (root->right->key < key)// Zag-Zag (Right Right) 
        { 
            // Bring the key as root of right-right and do first rotation 
            root->right->right = splay(root->right->right, key); 
            root = leftRotate(root); 
        } 
  
        // Do second rotation for root 
        return (root->right == NULL)? root: leftRotate(root); 
    } 
} 

// Create LRU cache utilizing a comparator
struct pmat_lru_cache *pmat_create_lru(void) {
    struct pmat_lru_cache *cache = VG_(malloc)("lru.cache", (SizeT)sizeof(struct pmat_lru_cache));
    cache->root = NULL;
    return cache;
}

// Insert key and value into LRU Cache.
void pmat_lru_cache_insert(struct pmat_lru_cache *cache, Addr key, void *value) {
    struct pmat_lru_node *node = create_lru_node(key, value);
    if (cache->root == NULL) {
        cache->root = node;
        return;
    }

    struct pmat_lru_node *found = splay(cache->root, key);
    if (found->key > key) {
        node->left = found->left;
        node->right = found;
        found->left = NULL;
        found->num_left = (found->left) ? (found->left->num_left + found->left->num_right + 1) : 0;
        found->num_right = (found->right) ? (found->right->num_left + found->right->num_right + 1) : 0; 
        node->num_left = (node->left) ? (node->left->num_left + node->left->num_right + 1) : 0;
        node->num_right = (node->right) ? (node->right->num_left + node->right->num_right + 1) : 0;
    } else if (found->key < key) {
        node->left = found;
        node->right = found->right;
        found->right = NULL;
        found->num_left = (found->left) ? (found->left->num_left + found->left->num_right + 1) : 0;
        found->num_right = (found->right) ? (found->right->num_left + found->right->num_right + 1) : 0; 
        node->num_left = (node->left) ? (node->left->num_left + node->left->num_right + 1) : 0;
        node->num_right = (node->right) ? (node->right->num_left + node->right->num_right + 1) : 0;
    } else {
        // Update...
        found->value = value;
    }
}

// Evicts an entry from the LRU Cache; returns evicted value
void *pmat_lru_cache_evict(struct pmat_lru_cache *cache, UInt *seed) {
    // Stochastic traversal...
    struct pmat_lru_node *node = cache->root;
    struct pmat_lru_node *parent = NULL;
    tl_assert2(node != NULL, "Attempt to evict from a cache that is empty!");
    Bool found = False;
    while (True) {
        // Leaf node? Done!
        if (node->num_left == 0 && node->num_right == 0) {
            break;
        }
        parent = node;
        UInt x = VG_(random)(seed) % 100;
        UInt total = node->num_left + node->num_right;
        UInt probLeft = (node->num_left / ((Double) total)) * 100;
        if (x <= probLeft) {
            tl_assert2(node->left != NULL, "Somehow %d with NULL left children!", node->num_left);
            node = node->left;
        } else {
            tl_assert2(node->left != NULL, "Somehow %d with NULL right children!", node->num_right);
            node = node->right;
        }
    }
    if (parent != NULL) {
        if (parent->left == node) {
            parent->left = NULL;
        } else if (parent->right == node) {
            parent->right = NULL;
        } else {
            tl_assert2(0, "Somehow parent->left nor parent->right is equal to node!");
        }
    }
    void *retval = node->value;
    VG_(free)(node->value);
    return retval;
}

// Searches the LRU cache; if present, returns value and splays; returns NULL if not found
// We only splay if it is a hit; we do not want transient stores to influence cache..
// or induce unnecessary overhead
void *pmat_lru_cache_lookup(struct pmat_lru_cache *cache, Addr key) {
    struct pmat_lru_node *node = cache->root;
    Bool found = False;
    while (node != NULL) {
        if (node->key == key) {
            found = True;
            break;
        } else {
            if (node->key > key) {
                node = node->left;
            } else {
                node = node->right;
            }
        }
    }

    if (!found) return NULL;
    void *retval = node->value;
    cache->root = splay(cache->root, key);
    return retval;
}

// Obtains the size of the LRU Cache
Int pmat_lru_cache_size(struct pmat_lru_cache *cache) {
    return (cache->root) ? (cache->root->num_left + cache->root->num_right + 1) : 0;
}
