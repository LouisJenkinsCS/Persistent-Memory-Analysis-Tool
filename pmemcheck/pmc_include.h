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

#ifndef PMC_INCLUDE_H
#define PMC_INCLUDE_H

/** Single store to memory. */
struct pmem_st {
    Addr addr;
    ULong size;
    ULong block_num;
    UWord value;
    ExeContext *context;
    enum store_state {
        STST_CLEAN,
        STST_DIRTY,
        STST_FLUSHED,
    } state;
};

struct pmat_cache_entry {
    // Bitmap to keep track of dirty bits
    ExeContext *lastPendingStore;
    Long dirtyBits;
    Addr addr;
    UChar data[0];
};

typedef int (*pmat_verification_fn)(void *, SizeT sz);

struct pmat_registered_file {
    char *name;
    UWord descr;
    Addr addr; 
    UWord size;
    pmat_verification_fn verify; 
};

struct pmat_write_buffer_entry {
    struct pmat_cache_entry *entry;
    ThreadId tid; 
};

// Converts addr to cache line addr
#define CACHELINE_SIZE 64
#define TRIM_CACHELINE(addr) ((addr) &~ (CACHELINE_SIZE - 1))
#define OFFSET_CACHELINE(addr) ((addr) % CACHELINE_SIZE)
#define NUM_CACHE_ENTRIES 1024
#define NUM_WB_ENTRIES 64

/*
    sys/waitstatus.h constants for portability (only on Linux, of course...)
*/
#if defined(VGO_linux)
#define VKI_WCOREFLAG 0x80

/* If WIFEXITED(STATUS), the low-order 8 bits of the status.  */
#define VKI_WEXITSTATUS(status) (((status) & 0xff00) >> 8)

/* If WIFSIGNALED(STATUS), the terminating signal.  */
#define VKI_WTERMSIG(status) ((status) & 0x7f)

/* If WIFSTOPPED(STATUS), the signal that stopped the child.  */
#define VKI_WSTOPSIG(status) VKI_WEXITSTATUS(status)

/* Nonzero if STATUS indicates normal termination.  */
#define VKI_WIFEXITED(status) (VKI_WTERMSIG(status) == 0)

/* Nonzero if STATUS indicates termination by a signal.  */
#define VKI_WIFSIGNALED(status) \
  (((signed char) (((status) & 0x7f) + 1) >> 1) > 0)

#define VKI_WCOREDUMP(status) ((status) & VKI_WCOREFLAG)

#else
#error Unknown OS
#endif

/*------------------------------------------------------------*/
/*--- Common functions                                     ---*/
/*------------------------------------------------------------*/

/* Check if the given region is in the set. */
UWord is_in_mapping_set(const struct pmem_st *region, OSet *region_set);

/* Add a region to a set. */
void add_region(const struct pmem_st *region, OSet *region_set);

/* Remove a region from a set. */
void remove_region(const struct pmem_st *region, OSet *region_set);

/* A compare function for regions stored in the OSetGen. */
Word cmp_pmem_st(const void *key, const void *elem);

Word cmp_pmat_cache_entries(const void *key, const void *elem);

Word cmp_pmat_registered_files1(const void *key, const void *elem);

Word cmp_pmat_registered_files2(const void *key, const void *elem);

Word cmp_pmat_write_buffer_entries(const void *key, const void *elem);

/* Check and update the given warning event register. */
void add_warning_event(struct pmem_st **event_register, UWord *nevents,
                  struct pmem_st *event, UWord limit, void (*err_msg)(UWord));

/* Check if regions overlap */
UWord check_overlap(const struct pmem_st *lhs, const struct pmem_st *rhs);

/*------------------------------------------------------------*/
/*--- Transactions related                                 ---*/
/*------------------------------------------------------------*/

/* Initialize the transactions module */
void init_transactions(Bool transactions_only);

/* Add a new transaction */
void register_new_tx(UWord tx_id);

/* Remove a transaction */
UInt remove_tx(UWord tx_id);

/* Add a memory region to a transaction */
UInt add_obj_to_tx(UWord tx_id, UWord base, UWord size);

/* Remove a registered region from the given transaction */
UInt remove_obj_from_tx(UWord tx_id, UWord base, UWord size);

/* Explicitely add a thread to a transaction */
UInt add_thread_to_tx(UWord tx_id);

/* Explicitely remove a thread from a transaction */
UInt remove_thread_from_tx(UWord tx_id);

/* Exclude a memory region from analysis */
void add_to_global_excludes(const struct pmem_st *region);

/* Handle the store made to PMEM in regards to running transactions */
void handle_tx_store(const struct pmem_st *store);

/* Print the summary of transaction analysis */
void print_tx_summary(void);

/* Return all errors referenced to transactions */
UWord get_tx_all_err(void);

#endif	/* PMC_INCLUDE_H */

