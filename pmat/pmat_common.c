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
    const struct pmat_write_buffer_entry *lhs = (const struct pmat_write_buffer_entry *) (key);
    const struct pmat_write_buffer_entry *rhs = (const struct pmat_write_buffer_entry *) (elem);

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