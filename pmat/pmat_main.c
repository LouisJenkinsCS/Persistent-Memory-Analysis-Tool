/*
 * Persistent memory checker.
 * Copyright (c) 2014-2016, Intel Corporation.
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

/*
 * This program is based on lackey, cachegrind and memcheck.
 */
#include <sys/param.h>
#include <sys/ipc.h>
#include "pub_tool_libcfile.h"
#include "pub_tool_libcproc.h"
#include <fcntl.h>
#include "pub_tool_oset.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_debuginfo.h"
#include "pmat.h"
#include "pmat_include.h"
#include "pub_tool_vki.h"

/* track at max this many multiple overwrites */
#define MAX_MULT_OVERWRITES 10000UL

/* track at max this many flush error events */
#define MAX_FLUSH_ERROR_EVENTS 10000UL

/* build various kinds of expressions */
#define triop(_op, _arg1, _arg2, _arg3) \
                                 IRExpr_Triop((_op),(_arg1),(_arg2),(_arg3))
#define binop(_op, _arg1, _arg2) IRExpr_Binop((_op),(_arg1),(_arg2))
#define unop(_op, _arg)          IRExpr_Unop((_op),(_arg))
#define mkU1(_n)                 IRExpr_Const(IRConst_U1(_n))
#define mkU8(_n)                 IRExpr_Const(IRConst_U8(_n))
#define mkU16(_n)                IRExpr_Const(IRConst_U16(_n))
#define mkU32(_n)                IRExpr_Const(IRConst_U32(_n))
#define mkU64(_n)                IRExpr_Const(IRConst_U64(_n))
#define mkexpr(_tmp)             IRExpr_RdTmp((_tmp))

/** Max store size */
#define MAX_DSIZE    256

/** Max allowable path length */
#define MAX_PATH_SIZE 4096

/** Holds parameters and runtime data */
static struct pmem_ops {
    const HChar *pmat_eviction_policy_str;
    /** Eviction policy being used. */
    struct pmat_eviction_policy pmat_eviction_policy;
    /** Mappings of files addresses to their descriptors */
    OSet *pmat_registered_files;
    /** Store buffer for to-be-written-back stores. */
    OSet *pmat_writeback_buffer_entries;
    /** Aggregate dumps (null if pmat_aggregate_dump_only is false) (Cache-Only) */
    OSet *pmat_aggregate_cache_dump;
    OSet *pmat_aggregate_flushed_dump;
    /** Whether or not we should verify */
    Bool pmat_should_verify;
    /** Verification program */
    const HChar *pmat_verifier;
    /** Whether or not a copy of the shadow region (binary) should be preserved on error. */
    Bool pmat_preserve_bin_on_error;
    /** Whether to create a single aggregated .dump file or not on exit. No .stderr or .stdout files are created if so. */
    Bool pmat_aggregate_dump_only;
    /** Whether to terminate on the first error or not. */
    Bool pmat_terminate_on_error;
    /** Set of addresses to ignore (marked transient) */
    OSet *pmat_transient_addresses; 
    /** Random Seed used for RNG. */
    UInt pmat_rng_seed;
    /** Probability of random eviction... Defaults to 0.5. */
    Double pmat_eviction_prob;
    /** Probability of crash occurring... Defaults to 0.01. */
    Double pmat_crash_prob;
    /** Lowest probability of crash occurring... defaults 0.5 * base probability */
    Double pmat_min_crash_prob;
    /** Highest probability of crash occurring... defaults to 1.5 * base probability */
    /** Number of cache entries... Defaults to 1024 * 1024 (64MBs of Cache) */
    Word pmat_num_cache_entries;
    /** Number of write-back reordering buffer entries... Defaults to 128 * 1024 (8MBs of Cache) */
    Word pmat_num_wb_entries;
    /** Number of verifications that have been run so far. */
    Word num_verifications;
    /** Number of bad verifications. */
    Word num_bad_verifications;
    /** Average nanoseconds per verification call*/
    Double average_verification_time;
    /** Minimum nanoseconds per verification call*/
    Double min_verification_time;
    /** Maximum nanoseconds per verification call*/
    Double max_verification_time;
    /** Mean nanoseconds per verification call*/
    Double mean_verification_time;
    /** Sum-of-Squares-of-Differences nanoseconds per verification call*/
    Double ssd_verification_time;
} pmem;

struct pmat_flush_location {
    ExeContext *store;
    ExeContext *flush;
};

/*
 * Memory tracing pattern as in cachegrind/lackey - in case of future
 * improvements.
 */

/** A specific kind of expression. */
typedef IRExpr IRAtom;

/** Types of discernable events. */
typedef enum {
    Event_Ir,
    Event_Dr,
    Event_Dw,
    Event_Dm
} EventKind;

/** The event structure. */
typedef struct {
    EventKind ekind;
    IRAtom *addr;
    SizeT size;
    IRAtom *guard; /* :: Ity_I1, or NULL=="always True" */
    IRAtom *value;
} Event;

/** Number of sblock run. */
static ULong sblocks = 0;

static void stringify_stack_trace(ExeContext *context, int fd);
static Int cmp_exe_context_pointers(const ExeContext **lhs, const ExeContext **rhs);
static void maybe_simulate_crash(void);


static Int cmp_flush_locations(struct pmat_flush_location *loc1, struct pmat_flush_location *loc2) {
    Int retval = cmp_exe_context_pointers(&loc1->store, &loc2->store);
    if (retval != 0) {
        if (loc1->flush == NULL && loc2->flush == NULL) {
            return 0;
        } else if ((loc1-> flush == NULL && loc2->flush != NULL)) {
            return 1;
        } else if ((loc1->flush != NULL && loc2->flush == NULL)) {
            return -1;
        } else {
            return cmp_exe_context_pointers(&loc1->flush, &loc2->flush);
        }
    } else {
        return retval;
    }
}

static void *eviction_lookup(Addr key) {
    return pmem.pmat_eviction_policy.lookup(pmem.pmat_eviction_policy.arg, key);
}

static void eviction_insert(Addr key, void *value) {
    pmem.pmat_eviction_policy.insert(pmem.pmat_eviction_policy.arg, key, value);
}

static void eviction_remove(Addr key) {
    pmem.pmat_eviction_policy.remove(pmem.pmat_eviction_policy.arg, key);
}

static void *eviction_evict(void) {
    return pmem.pmat_eviction_policy.evict(pmem.pmat_eviction_policy.arg);
}

static UInt eviction_size(void) {
    return pmem.pmat_eviction_policy.size(pmem.pmat_eviction_policy.arg);
}

static void **eviction_to_array(SizeT *sz) {
    return pmem.pmat_eviction_policy.to_array(pmem.pmat_eviction_policy.arg, sz);
}

static UInt get_random(void) {
    return VG_(random)(&pmem.pmat_rng_seed);
}

// Obtain number of processors
static Int get_num_procs(void) {
    /* the assumed cache line size */
    Int ret_val = -1;

    int fp;
    if ((fp = VG_(fd_open)("/proc/cpuinfo",O_RDONLY, 0)) < 0) {
        return ret_val;
    }

    int proc_read_size = 2048;
    char read_buffer[proc_read_size];

    while (VG_(read)(fp, read_buffer, proc_read_size - 1) > 0) {
        static const char procs[] = "cpu cores\t: ";
        read_buffer[proc_read_size] = 0;

        char *cache_str = NULL;
        if ((cache_str = VG_(strstr)(read_buffer, procs)) != NULL) {
            /* move to cache line size */
            cache_str += sizeof (procs) - 1;
            ret_val = VG_(strtoll10)(cache_str, NULL) ? : 64;
            break;
        }
    }

    VG_(close)(fp);

    if (ret_val == -1) {
        VG_(emit)("Unable to read /proc/cpuinfo, falling back to single-core...");
        return 1;
    }
    return ret_val;
}

// Update statistics for nanoseconds per verification call
static void update_stats(Double sec) {
    Double delta1 = sec - pmem.mean_verification_time;
    pmem.mean_verification_time += delta1 / ((Double) pmem.num_verifications);
    Double delta2 = sec - (Double) pmem.mean_verification_time;
    pmem.ssd_verification_time += delta1 * delta2;
}

static void get_stats(Double *mean, Double *variance) {
    *mean = pmem.mean_verification_time;
    *variance = pmem.ssd_verification_time / (Double) pmem.num_verifications;
}

// Comparator for finding a file associated with a name
static Int find_file_by_name(const struct pmat_registered_file *lhs, const struct pmat_registered_file *rhs) {
    return VG_(strcmp)(lhs->name, rhs->name);
}

// Comparator for finding a file associated with an address
static Int find_file_by_addr(const struct pmat_registered_file *lhs, const struct pmat_registered_file *rhs) {
    if (rhs->size == 0) {
        // LHS should have a non-zero size...
        tl_assert2(lhs->size, "LHS(addr:0x%lx) has size of 0...", lhs->addr);
        if (rhs->addr < lhs->addr) {
            return -1;
        } else if (rhs->addr > lhs->addr + lhs->size) {
            return 1;
        } else {
            return 0;
        }
    } else if (lhs->size == 0) {
        if (lhs->addr < rhs->addr) {
            return 1;
        } else if (lhs->addr > rhs->addr + rhs->size) {
            return -1;
        } else {
            return 0;
        }
    } else {
        // Neither lhs nor rhs has size of 0, meaning it is not finding a file... 
        // Wrong comparator?
        tl_assert2(0, "LHS(addr:0x%lx, size:0x%lx) and RHS(addr:0x%lx, size:0x%lx) have non-zero sizes...", lhs->addr, lhs->size, rhs->addr, rhs->size);
    }
}


/**
* \brief Check if a given store overlaps with registered persistent memory
*        regions.
* \param[in] addr The base address of the store.
* \param[in] size The size of the store.
* \return True if store overlaps with any registered region, false otherwise.
*/
static Bool
is_pmem_access(Addr addr, SizeT size)
{
    if (VG_(OSetGen_Size)(pmem.pmat_registered_files) == 0) {
        return False;
    }

    struct pmat_registered_file file = {0};
    file.addr = addr;
    Bool found = !!VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
    if (found) {
        // Check if it is transient...
        if (VG_(OSetGen_Size)(pmem.pmat_transient_addresses) == 0) {
            return True;
        } else {
            struct pmat_transient_entry trans = {0};
            trans.addr = addr;
            trans.size = size;
            if (VG_(OSetGen_Lookup)(pmem.pmat_transient_addresses, &trans)) {
                return False;
            } else {
                return True;
            }
        }
    }
    return False;
}

static void do_writeback(struct pmat_cache_entry *entry, Bool explicit);
static void dump(void);

static void write_to_file(struct pmat_writeback_buffer_entry *entry) {
    // Find the file associated with it...
    struct pmat_registered_file file = {0};
    file.addr = entry->entry->addr; 
    struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
    
    // TODO: May want to move this behind some compile-time preprocessor directive
    // Check to see if file exists...
    if (!realFile) {
        VG_(emit)("Could not find descriptor for 0x%lx\n", file.addr);
        VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
        struct pmat_registered_file *tmp;
        while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
            VG_(emit)("File Found: (%lx, 0x%lx, 0x%lx)\n", tmp->descr, tmp->addr, tmp->size);
        }
    }
    tl_assert(realFile && "Unable to find descriptor associated with an address!");

    UChar *bytes = (void *) (realFile->mmap_addr + (entry->entry->addr - realFile->addr));
    for (ULong i = 0; i < CACHELINE_SIZE; i++) {
        ULong bit = (entry->entry->dirtyBits & (1ULL << i));
        if (bit) {
            bytes[i] = entry->entry->data[i];
        }
    }
    maybe_simulate_crash();
}

/**
 * \brief Prints registered store statistics.
 *
 * \details Print outstanding stores which were not made persistent during the
 * whole run of the application.
 */
static void
print_store_stats(void)
{
    dump();
    VG_(umsg)("%ld out of %ld verifications failed...\n", pmem.num_bad_verifications, pmem.num_verifications);
}

/**
 * \brief Check if a memcpy/memset is at the given instruction address.
 *
 * \param[in] ip The instruction address to check.
 * \return True if the function name has memcpy/memset in its name,
 *         False otherwise.
 */
static Bool
is_ip_memset_memcpy(Addr ip)
{
    DiEpoch ep = VG_(current_DiEpoch)();
    InlIPCursor *iipc = VG_(new_IIPC)(ep, ip);
    const HChar *buf = VG_(describe_IP)(ep, ip,  iipc);
    Bool present = (VG_(strstr)(buf, "memcpy") != NULL);
    present |= (VG_(strstr)(buf, "memset") != NULL);
    VG_(delete_IIPC)(iipc);
    return present;
}

static Int
cmp_exe_context_pointers(const ExeContext **lhs, const ExeContext **rhs) {
    tl_assert(lhs && *lhs && rhs && *rhs);

    if (lhs == rhs || *lhs == *rhs)
        return 0;

    /* retrieve stacktraces */
    UInt n_ips1;
    UInt n_ips2;
    const Addr *ips1 = VG_(make_StackTrace_from_ExeContext)(*lhs, &n_ips1);
    const Addr *ips2 = VG_(make_StackTrace_from_ExeContext)(*rhs, &n_ips2);

    /* Must be at least one address in each trace. */
    tl_assert(n_ips1 >= 1 && n_ips2 >= 1);

    /* different stacktrace depth */
    if (n_ips1 > n_ips2) return 1;
    else if (n_ips2 > n_ips1) return -1;

    /* omit memcpy/memset at the top of the callstack */
    Int i = 0;
    if ((ips1[0] == ips2[0])
            || (is_ip_memset_memcpy(ips1[0]) && is_ip_memset_memcpy(ips2[0])))
        ++i;
    /* compare instruction pointers */
    for (; i < n_ips1; i++) {
        if (ips1[i] > ips2[i]) return 1;
        else if (ips2[i] > ips1[i]) return -1;
    }

    return 0;
}

static void dump_to_file(int fd) {
    SizeT size;
    void **cache_lines = eviction_to_array(&size);
    HChar charbuf[256];
    VG_(snprintf)(charbuf, 256, "Number of cache-lines not made persistent: %u\n", size);
    VG_(write)(fd, charbuf, VG_(strlen(charbuf)));

    // To prevent having to print out ExeContext for cache lines with the same stack
    // trace, we instead create mappings from stack traces to cache lines.
    OSet *unique_cache_lines = VG_(OSetGen_Create)(0, (OSetCmp_t) cmp_exe_context_pointers, VG_(malloc), "Coalesce Cache Lines", VG_(free));
    struct pmat_cache_entry *entry;
    for (SizeT i = 0; i < size; i++) {
        entry = cache_lines[i];
        if (VG_(OSetGen_Contains)(unique_cache_lines, &entry->locOfStore)) continue;
        ExeContext **node = VG_(OSetGen_AllocNode)(unique_cache_lines, (SizeT) sizeof(ExeContext *));
        *node = entry->locOfStore;
        VG_(OSetGen_Insert)(unique_cache_lines, node);
        struct pmat_registered_file file = {0};
        file.addr = entry->addr;
        struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
        if (!realFile) {
            VG_(emit)("Could not find descriptor for 0x%lx\n", file.addr);
            VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
            struct pmat_registered_file *tmp;
            while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
                VG_(emit)("File Found: (%lx, 0x%lx, 0x%lx)\n", tmp->descr, tmp->addr, tmp->size);
            }
        }
        tl_assert(realFile);


        VG_(snprintf)(charbuf, 256, "['%s']\n", realFile->name);
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        stringify_stack_trace(entry->locOfStore, fd);
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    }

    VG_(OSetGen_Destroy)(unique_cache_lines);
    unique_cache_lines = VG_(OSetGen_Create)(0, (OSetCmp_t) cmp_flush_locations, VG_(malloc), "Coalesce Cache Lines", VG_(free));

    VG_(snprintf)(charbuf, 256, "Number of cache-lines flushed but not fenced: %u\n", VG_(OSetGen_Size)(pmem.pmat_writeback_buffer_entries));
    VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    VG_(OSetGen_ResetIter)(pmem.pmat_writeback_buffer_entries);
    struct pmat_writeback_buffer_entry *wbentry = NULL;
    while ((wbentry = VG_(OSetGen_Next)(pmem.pmat_writeback_buffer_entries))) {
        struct pmat_flush_location tmp = { .store = wbentry->entry->locOfStore, .flush = wbentry->locOfFlush };
        if (VG_(OSetGen_Contains)(unique_cache_lines, &tmp)) continue;
        struct pmat_flush_location *node = VG_(OSetGen_AllocNode)(unique_cache_lines, (SizeT) sizeof(struct pmat_flush_location));
        *node = tmp;
        VG_(OSetGen_Insert)(unique_cache_lines, node);
        struct pmat_cache_entry *_entry = wbentry->entry;
        struct pmat_registered_file file = {0};
        file.addr = _entry->addr;
        struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
        tl_assert(realFile);
        VG_(snprintf)(charbuf, 256, "['%s']\n", realFile->name);
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        VG_(snprintf)(charbuf, 256, "~~~~~~(Location of Store)~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        stringify_stack_trace(_entry->locOfStore, fd);
        VG_(snprintf)(charbuf, 256, "~~~~~~(Location of Flush)~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        if (wbentry->locOfFlush) {
            stringify_stack_trace(wbentry->locOfFlush, fd);
        } else {
            VG_(snprintf)(charbuf, 256, "(EVICTED)\n");
            VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        }
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    }
}

static void dump_aggregate_to_file(void) {
    SysRes res = VG_(open)("aggregate.dump", VKI_O_CREAT | VKI_O_TRUNC | VKI_O_RDWR, 0666);
    if (sr_isError(res)) {
        VG_(emit)("Could not open file '%s'; errno: %ld\n", "aggregate.dump", sr_Err(res));
        tl_assert(0);
    }
    int fd = sr_Res(res);
    HChar charbuf[256];
    VG_(snprintf)(charbuf, 256, "Number of distinct cache-lines not made persistent: %u\n", VG_(OSetGen_Size)(pmem.pmat_aggregate_cache_dump));
    VG_(write)(fd, charbuf, VG_(strlen(charbuf)));

    ExeContext *entry;
    VG_(OSetGen_ResetIter)(pmem.pmat_aggregate_cache_dump);
    while ((entry = VG_(OSetGen_Next)(pmem.pmat_aggregate_cache_dump))) {
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        stringify_stack_trace(entry, fd);
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    }

    VG_(snprintf)(charbuf, 256, "Number of distinct cache-lines flushed but not fenced: %u\n", VG_(OSetGen_Size)(pmem.pmat_aggregate_flushed_dump));
    VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    VG_(OSetGen_ResetIter)(pmem.pmat_aggregate_flushed_dump);
    struct pmat_flush_location *flushLoc = NULL;
    while ((flushLoc = VG_(OSetGen_Next)(pmem.pmat_aggregate_flushed_dump))) {
        VG_(snprintf)(charbuf, 256, "~~~~~~(Location of Store)~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        stringify_stack_trace(flushLoc->store, fd);
        VG_(snprintf)(charbuf, 256, "~~~~~~(Location of Flush)~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        if (flushLoc->flush) {
            stringify_stack_trace(flushLoc->flush, fd);
        } else {
            VG_(snprintf)(charbuf, 256, "(EVICTED)\n");
            VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        }
        VG_(snprintf)(charbuf, 256, "~~~~~~~~~~~~~~~\n");
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
    }
}

static void dump_aggregate(void) {
    SizeT size;
    void **cache_lines = eviction_to_array(&size);
    
    // To prevent having to print out ExeContext for cache lines with the same stack
    // trace, we instead create mappings from stack traces to cache lines.
    struct pmat_cache_entry *entry;
    for (SizeT i = 0; i < size; i++) {
        entry = cache_lines[i];
        if (VG_(OSetGen_Contains)(pmem.pmat_aggregate_cache_dump, &entry->locOfStore)) continue;
        ExeContext **node = VG_(OSetGen_AllocNode)(pmem.pmat_aggregate_cache_dump, (SizeT) sizeof(ExeContext *));
        *node = entry->locOfStore;
        VG_(OSetGen_Insert)(pmem.pmat_aggregate_cache_dump, node);
    }

    VG_(OSetGen_ResetIter)(pmem.pmat_writeback_buffer_entries);
    struct pmat_writeback_buffer_entry *wbentry = NULL;
    while ((wbentry = VG_(OSetGen_Next)(pmem.pmat_writeback_buffer_entries))) {
        struct pmat_flush_location tmp = { .store = wbentry->entry->locOfStore, .flush = wbentry->locOfFlush };
        if (VG_(OSetGen_Contains)(pmem.pmat_aggregate_flushed_dump, &tmp)) continue;
        struct pmat_flush_location *node = VG_(OSetGen_AllocNode)(pmem.pmat_aggregate_flushed_dump, (SizeT) sizeof(struct pmat_flush_location));
        *node = tmp;
        VG_(OSetGen_Insert)(pmem.pmat_aggregate_flushed_dump, node);
    }
}

static void dump(void) {
    SizeT size;
    void **cache_lines = eviction_to_array(&size);
    VG_(umsg)("Number of cache-lines not made persistent: %u\n", size);

    // To prevent having to print out ExeContext for cache lines with the same stack
    // trace, we instead create mappings from stack traces to cache lines.
    OSet *unique_cache_lines = VG_(OSetGen_Create)(0, (OSetCmp_t) cmp_exe_context_pointers, VG_(malloc), "Coalesce Cache Lines", VG_(free));
    struct pmat_cache_entry *entry;
    for (SizeT i = 0; i < size; i++) {
        entry = cache_lines[i];
        if (VG_(OSetGen_Contains)(unique_cache_lines, &entry->locOfStore)) continue;
        ExeContext **node = VG_(OSetGen_AllocNode)(unique_cache_lines, (SizeT) sizeof(ExeContext *));
        *node = entry->locOfStore;
        VG_(OSetGen_Insert)(unique_cache_lines, node);
        struct pmat_registered_file file = {0};
        file.addr = entry->addr;
        struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
        if (!realFile) {
            VG_(emit)("Could not find descriptor for 0x%lx\n", file.addr);
            VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
            struct pmat_registered_file *tmp;
            while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
                VG_(emit)("File Found: (%lx, 0x%lx, 0x%lx)\n", tmp->descr, tmp->addr, tmp->size);
            }
        }
        tl_assert(realFile);
        VG_(umsg)("['%s']\n", realFile->name);
        VG_(umsg)("~~~~~~~~~~~~~~~\n");
        VG_(pp_ExeContext)(entry->locOfStore);
        VG_(umsg)("~~~~~~~~~~~~~~~\n");
    }

    VG_(OSetGen_Destroy)(unique_cache_lines);
    unique_cache_lines = VG_(OSetGen_Create)(0, (OSetCmp_t) cmp_flush_locations, VG_(malloc), "Coalesce Cache Lines", VG_(free));

    VG_(umsg)("Number of cache-lines flushed but not fenced: %u\n", VG_(OSetGen_Size)(pmem.pmat_writeback_buffer_entries));
    VG_(OSetGen_ResetIter)(pmem.pmat_writeback_buffer_entries);
    struct pmat_writeback_buffer_entry *wbentry = NULL;
    while ((wbentry = VG_(OSetGen_Next)(pmem.pmat_writeback_buffer_entries))) {
        struct pmat_flush_location tmp = { .store = wbentry->entry->locOfStore, .flush = wbentry->locOfFlush };
        if (VG_(OSetGen_Contains)(unique_cache_lines, &tmp)) continue;
        struct pmat_flush_location *node = VG_(OSetGen_AllocNode)(unique_cache_lines, (SizeT) sizeof(struct pmat_flush_location));
        *node = tmp;
        VG_(OSetGen_Insert)(unique_cache_lines, node);
        struct pmat_cache_entry *_entry = wbentry->entry;
        struct pmat_registered_file file = {0};
        file.addr = _entry->addr;
        struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
        tl_assert(realFile);
        VG_(umsg)("Leaked Cache-Line at address 0x%lx belonging to file '%s'\n", _entry->addr, realFile->name);
        VG_(umsg)("~~~~~~(Location of Store)~~~~~~~~~\n");
        VG_(pp_ExeContext)(_entry->locOfStore);
        VG_(umsg)("~~~~~~(Location of Flush)~~~~~~~~~\n");
        if (wbentry->locOfFlush) {
            VG_(pp_ExeContext)(wbentry->locOfFlush);
        } else {
            VG_(umsg)("(EVICTED)\n");
        }
        VG_(umsg)("~~~~~~~~~~~~~~~\n");
    }
}

static Bool exec(const char *cmd, const char **args) {
    Int pid = VG_(fork)();
    if (pid == 0) {
        // Child
        VG_(execv)(cmd, args); 
        // failed?
        VG_(exit)(-1);
    } else {
        Int retval;
        VG_(waitpid)(pid, &retval, 0);
        return VKI_WIFEXITED(retval) && VKI_WEXITSTATUS(retval) == 0;
    }
}

static void copy_file(const char *f1, const char *f2) {
    const char *args[5];
    args[0] = "cp";
    args[1] = f1;
    args[2] = f2;
    args[3] = "--reflink=auto";
    args[4] = NULL;
    exec("/bin/cp", args);
}

static void call_verifiers() {
    VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
    struct pmat_registered_file *tmp;
    while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
        tmp->verify_fn(tmp->mmap_addr, tmp->size);
    }
}

static void copy_files(const char *suffix) {
    VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
    struct pmat_registered_file *tmp;
    while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
        char file_name[1024];
        VG_(snprintf)(file_name, 1024, "%s.%s.%ld", tmp->name, suffix, pmem.num_verifications);
        copy_file(tmp->name, file_name);
    }
}

static void stringify_stack_trace_helper(UInt n, DiEpoch ep, Addr ip, void *fdptr) {
    int fd = *(int *)fdptr;
    char charbuf[256];
    InlIPCursor *iipc = VG_(new_IIPC)(ep, ip);

   do {
        const HChar *buf = VG_(describe_IP)(ep, ip, iipc);
        VG_(snprintf)(charbuf, 256, "   %s %s\n", (n == 0 ? "at" : "by"), buf);
        VG_(write)(fd, charbuf, VG_(strlen(charbuf)));
        n++; 
      // Increase n to show "at" for only one level.
   } while (VG_(next_IIPC)(iipc));
   VG_(delete_IIPC)(iipc);
}

static void stringify_stack_trace(ExeContext *context, int fd) {
    UInt n_ips;
    DiEpoch ep = VG_(get_ExeContext_epoch)(context);
    const Addr *ips = VG_(make_StackTrace_from_ExeContext)(context, &n_ips);

    int argfd = fd;
    VG_(apply_StackTrace)(stringify_stack_trace_helper, &argfd, ep, ips, n_ips);
}

// Returns seconds difference
static Double diff(struct vki_timespec start, struct vki_timespec end)
{
    struct vki_timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return (Double) (temp.tv_sec + ((Double) temp.tv_nsec) / 1000000000.0);
}

static void pmat_fini(int exitcode);

// TODO: Need to write stderr and stdout to their own temporary files; these files persist if recovery fails!
// TODO: Need to set timeout for recovery operations, in case they do an infinite loop. Parent currently gets stuck in a syscall!
static void simulate_crash(void) {
    if (!pmem.pmat_verifier) {
        VG_(fmsg)("[Error] Attempt to force a crash without a verification function!\n");
        return;
    } else if (VG_(OSetGen_Size)(pmem.pmat_registered_files) == 0) {
        VG_(fmsg)("[Error] Attempt to force a crash without registering persistent region!\n");
        return;
    }

    Int verif_num = ++pmem.num_verifications;

    // Start timer...
    struct vki_timespec start;
    struct vki_timespec end;
    tl_assert2(VG_(clock_gettime)(VKI_CLOCK_MONOTONIC, &start) == 0, "Failed to get start time!");
    
    Int pid = VG_(fork)();
    if (pid != 0) {
        // parent
        Int retval;
        Int retpid = VG_(waitpid)(pid, &retval, 0);
        tl_assert2(VG_(clock_gettime)(VKI_CLOCK_MONOTONIC, &end) == 0, "Failed to get end time!");
        tl_assert2(pid == retpid, "waitpid(%d) returned unexpected pid %d", pid, retpid);

        Double sec = diff(start, end);
        update_stats(sec);
        pmem.max_verification_time = VG_MAX(pmem.max_verification_time, sec);
        pmem.min_verification_time = VG_MIN(pmem.min_verification_time, sec);
        if (pmem.min_verification_time == 0) pmem.min_verification_time = sec;

        // Check if child exited normally...
        if (VKI_WIFEXITED(retval) && VKI_WEXITSTATUS(retval) == 0) {
            // Normal exit; delete .stdout and .stderr
            char stderr_file[64];
            char stdout_file[64];

            VG_(snprintf)(stderr_file, 64, "%d.stderr", verif_num);
            VG_(snprintf)(stdout_file, 64, "%d.stdout", verif_num);
            VG_(unlink)(stderr_file);
            VG_(unlink)(stdout_file);
        } else {
            // Create copy of shadow region
            if (pmem.pmat_preserve_bin_on_error) {
                char bin_name[64];
                VG_(snprintf)(bin_name, 64, "%d", verif_num);
                copy_files(bin_name);
            }
            // Should we aggregate the dump file?
            if (pmem.pmat_aggregate_dump_only) {
                dump_aggregate();
            } else {
                char dump_file[64];
                VG_(snprintf)(dump_file, 64, "%d.dump", verif_num);
                SysRes res = VG_(open)(dump_file, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_RDWR, 0666);
                if (sr_isError(res)) {
                    VG_(emit)("Could not open file '%s'; errno: %ld\n", dump_file, sr_Err(res));
                    tl_assert(0);
                }
                dump_to_file(sr_Res(res));
            }

            pmem.num_bad_verifications++;
            if (pmem.pmat_terminate_on_error) {
                VG_(show_sched_status)(False, False, False);
                pmat_fini(1);
                VG_(exit)(1);
            }
        } 
        return;
    } else {
        // child...
        int numFiles = VG_(OSetGen_Size)(pmem.pmat_registered_files);
        // Redirect to a file...
        char stderr_file[64];
        char stdout_file[64];
        if (pmem.pmat_aggregate_dump_only) {
            VG_(snprintf)(stderr_file, 64, "/dev/null");
            VG_(snprintf)(stdout_file, 64, "/dev/null");    
        } else {
            VG_(snprintf)(stderr_file, 64, "%d.stderr", verif_num);
            VG_(snprintf)(stdout_file, 64, "%d.stdout", verif_num);
        }
        SysRes res = VG_(open)(stderr_file, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_RDWR, 0666);
        if (sr_isError(res)) {
            VG_(emit)("Could not open file '%s'; errno: %ld\n", stderr_file, sr_Err(res));
            tl_assert(0);
        }
        VG_(close)(2);
        VG_(dup2)(2, sr_Res(res));
        res = VG_(open)(stdout_file, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_RDWR, 0666);
        if (sr_isError(res)) {
            VG_(emit)("Could not open file '%s'; errno: %ld\n", stdout_file, sr_Err(res));
            tl_assert(0);
        }
        VG_(close)(1);
        VG_(dup2)(1, sr_Res(res));
        const char *args[numFiles + 3]; 
        args[0] = pmem.pmat_verifier;
        char numFilesStr[3];
        VG_(snprintf)(numFilesStr, 3, "%d", numFiles);
        args[1] = numFilesStr;
        int n = 2;
        VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
        struct pmat_registered_file *file;
        while ((file = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
            args[n++] = file->name;
        }
        args[n] = NULL;
        VG_(execv)(pmem.pmat_verifier, args);
        VG_(exit)(-1);
    }
}

static void maybe_simulate_crash(void) {
    if (!pmem.pmat_should_verify || !pmem.pmat_verifier || VG_(OSetGen_Size)(pmem.pmat_registered_files) == 0) return;
    if ((get_random() % 100) <= (pmem.pmat_crash_prob * 100)) {
        simulate_crash();
    }
}


/**
* \brief Trace the given store if it was to any of the registered persistent
*        memory regions.
* \param[in] addr The base address of the store.
* \param[in] size The size of the store.
* \param[in] value The value of the store.
*/
static VG_REGPARM(3) void trace_pmem_store(Addr addr, SizeT size, UWord value)
{
    // Check if this is a store to registered memory
    if (LIKELY(!is_pmem_access(addr, size))) {
        return;
    }

    if (TRIM_CACHELINE(addr) != TRIM_CACHELINE(addr + size - 1)) {
        UWord pt1 = 64 - OFFSET_CACHELINE(addr);
        UWord pt2 = (size * 8) - pt1;
        
        //trace_pmem_store(addr, pt1, value & (1 << (sizeof(UWord) - pt1));
        //trace_pmem_store(addr, size - (64 - OFFSET_CACHELINE(addr)));
        VG_(emit)("pt1=%ld, pt2=%ld\n", pt1, pt2);
        VG_(emit)("Warning: Split cache lines are not supported: %lu and %lu not in same cache line... (%lld,%lld)\nMaybe split to %x and %x!\n", 
            addr, addr + size, TRIM_CACHELINE(addr), TRIM_CACHELINE(addr + size), (1 << (pt1 * 8)) - 1, (1 << pt2) - 1);
    }    
    ULong startOffset = OFFSET_CACHELINE(addr);
    ULong endOffset = OFFSET_CACHELINE(addr + size);
    if (OFFSET_CACHELINE(addr + size) == 0) endOffset = CACHELINE_SIZE;
    if (startOffset > endOffset) {
        VG_(emit)("Warning: Split cache lines are not supported: %lu and %lu not in same cache line... (%lld,%lld)", addr, addr + size, startOffset, endOffset);
        //return;
    }

    // If the cache line has not been written back, write it into that cache-line.
    struct pmat_cache_entry *exists = eviction_lookup(TRIM_CACHELINE(addr));
    if (exists) {
        VG_(memcpy)(exists->data + startOffset, &value, size);
        exists->locOfStore = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);
        exists->tid = VG_(get_running_tid)();
        // Set bits being written to as dirty...
        exists->dirtyBits |= ((1ULL << ((ULong) size)) - 1ULL) << startOffset;
        return;
    } else {
        // Create a new entry...
        struct pmat_cache_entry *new_entry = VG_(malloc)("pmat.new_entry", (SizeT) sizeof (struct pmat_cache_entry) + CACHELINE_SIZE);
        new_entry->locOfStore = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);
        new_entry->tid = VG_(get_running_tid)();
        new_entry->addr = TRIM_CACHELINE(addr);
        new_entry->dirtyBits = 0;
        VG_(memset)(new_entry->data, 0, CACHELINE_SIZE);
        VG_(memcpy)(new_entry->data + OFFSET_CACHELINE(addr), &value, size);
        new_entry->dirtyBits |= ((1ULL << ((ULong) size)) - 1ULL) << startOffset;
        eviction_insert(TRIM_CACHELINE(addr), new_entry);
        // Check if we need to evict...
        if (eviction_size() > pmem.pmat_num_cache_entries) {
            struct pmat_cache_entry *entry = eviction_evict();
            do_writeback(new_entry, False);
        }
    }
}

// Unused...
static VG_REGPARM(3) void trace_pmem_store_dword(Addr addr, UWord value1, UWord value2) {
    trace_pmem_store(addr, sizeofIRType(Ity_I64), value1);
    trace_pmem_store(addr, sizeofIRType(Ity_I64), value2);
}

/**
* \brief Register the entry of a new SB.
*
* Useful when handling implementation independent multiple writes under
* the same address.
*/
static void
add_one_SB_entered(void)
{
    ++sblocks;
}

/**
* \brief Make a new atomic expression from e.
*
* A very handy function to have for creating binops, triops and widens.
* \param[in,out] sb The IR superblock to which the new expression will be added.
* \param[in] ty The IRType of the expression.
* \param[in] e The new expression to make.
* \return The Rd_tmp of the new expression.
*/
static IRAtom *
make_expr(IRSB *sb, IRType ty, IRExpr *e)
{
    IRTemp t;
    IRType tyE = typeOfIRExpr(sb->tyenv, e);

    tl_assert(tyE == ty); /* so 'ty' is redundant (!) */

    t = newIRTemp(sb->tyenv, tyE);
    addStmtToIRSB(sb, IRStmt_WrTmp(t,e));

    return mkexpr(t);
}

/**
* \brief Check if the expression needs to be widened.
* \param[in] sb The IR superblock to which the expression belongs.
* \param[in] e The checked expression.
* \return True if needs to be widened, false otherwise.
*/
static Bool
tmp_needs_widen(IRType type)
{
    switch (type) {
        case Ity_I1:
        case Ity_I8:
        case Ity_I16:
        case Ity_I32:
            return True;

        default:
            return False;
    }
}

/**
* \brief Check if the const expression needs to be widened.
* \param[in] e The checked expression.
* \return True if needs to be widened, false otherwise.
*/
static Bool
const_needs_widen(IRAtom *e)
{
    /* make sure this is a const */
    if (e->tag != Iex_Const) return False;

    switch (e->Iex.Const.con->tag) {
        case Ico_U1:
        case Ico_U8:
        case Ico_U16:
        case Ico_U32:
        case Ico_U64:
            return True;

        default:
            return False;
    }
}

/**
* \brief Widen a given const expression to a word sized expression.
* \param[in] e The expression being widened.
* \return The widened const expression.
*/
static IRAtom *
widen_const(IRAtom *e)
{
    /* make sure this is a const */
    tl_assert(e->tag == Iex_Const);

    switch (e->Iex.Const.con->tag) {
        case Ico_U1:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U1);

        case Ico_U8:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U8);

        case Ico_U16:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U16);

        case Ico_U32:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U32);

        case Ico_U64:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U64);

        default:
            tl_assert(False); /* cannot happen */
    }
}

/**
* \brief A generic widening function.
* \param[in] sb The IR superblock to which the expression belongs.
* \param[in] e The expression being widened.
* \return The widening operation.
*/
static IROp
widen_operation(IRSB *sb, IRAtom *e)
{
    switch (typeOfIRExpr(sb->tyenv, e)) {
        case Ity_I1:
            return Iop_1Uto64;

        case Ity_I8:
            return Iop_8Uto64;

        case Ity_I16:
            return Iop_16Uto64;

        case Ity_I32:
            return Iop_32Uto64;

        default:
            tl_assert(False); /* cannot happen */
    }
}

/**
* \brief Handle wide sse operations.
* \param[in,out] sb The IR superblock to which add expressions.
* \param[in] end The endianess.
* \param[in] addr The expression with the address of the operation.
* \param[in] data The expression with the value of the operation.
* \param[in] guard The guard expression.
* \param[in] size The size of the operation.
*/
static void
handle_wide_expr(IRSB *sb, IREndness end, IRAtom *addr, IRAtom *data,
        IRAtom *guard, SizeT size)
{
    IROp mkAdd;
    IRType ty, tyAddr;
    void *helper = trace_pmem_store;
    const HChar *hname = "trace_pmem_store";

    ty = typeOfIRExpr(sb->tyenv, data);

    tyAddr = typeOfIRExpr(sb->tyenv, addr);
    mkAdd = tyAddr==Ity_I32 ? Iop_Add32 : Iop_Add64;
    tl_assert( tyAddr == Ity_I32 || tyAddr == Ity_I64 );
    tl_assert( end == Iend_LE || end == Iend_BE );

    Int i;
    Int parts = 0;
    /* These are the offsets of the parts in memory. */
    UInt offs[4];

    /* Various bits for constructing the 4/2 lane helper calls */
    IROp ops[4];
    IRDirty *dis[4];
    IRAtom *addrs[4];
    IRAtom *datas[4];
    IRAtom *eBiass[4];

    if (ty == Ity_V256) {
         /* V256-bit case -- phrased in terms of 64 bit units (Qs), with
           Q3 being the most significant lane. */

        ops[0] =Iop_V256to64_0;
        ops[1] =Iop_V256to64_1;
        ops[2] =Iop_V256to64_2;
        ops[3] = Iop_V256to64_3;

        if (end == Iend_LE) {
            offs[0] = 0; offs[1] = 8; offs[2] = 16; offs[3] = 24;
        } else {
            offs[3] = 0; offs[2] = 8; offs[1] = 16; offs[0] = 24;
        }

        parts = 4;
    } else if (ty == Ity_V128) {

        /* V128-bit case
           See comment in next clause re 64-bit regparms also, need to be
           careful about endianness */
        ops[0] =Iop_V128to64;
        ops[1] =Iop_V128HIto64;

        if (end == Iend_LE) {
            offs[0] = 0; offs[1] = 8;
        } else {
            offs[0] = 8; offs[1] = 0;
        }

        parts = 2;
    }

    for(i = 0; i < parts; ++i) {
        eBiass[i] = tyAddr == Ity_I32 ? mkU32(offs[i]) : mkU64(offs[i]);
        addrs[i] = make_expr(sb, tyAddr, binop(mkAdd, addr, eBiass[i]));
        datas[i] = make_expr(sb, Ity_I64, unop(ops[i], data));
        dis[i] = unsafeIRDirty_0_N(3/*regparms*/, hname,
                VG_(fnptr_to_fnentry)(helper), mkIRExprVec_3(addrs[i],
                mkIRExpr_HWord(size / parts), datas[i]));
        if (guard)
            dis[i]->guard = guard;

        addStmtToIRSB(sb, IRStmt_Dirty(dis[i]));
    }
}

/**
* \brief Add a guarded write event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] dsize The size of the operation.
* \param[in] guard The guard expression.
* \param[in] value The expression with the value of the operation.
*/
static void
add_event_dw_guarded(IRSB *sb, IRAtom *daddr, Int dsize, IRAtom *guard,
        IRAtom *value)
{
    tl_assert(isIRAtom(daddr));
    tl_assert(isIRAtom(value));
    tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

    const HChar *helperName = "trace_pmem_store";
    void *helperAddr = trace_pmem_store;
    IRExpr **argv;
    IRDirty *di;
    IRType type = typeOfIRExpr(sb->tyenv, value);

    if (value->tag == Iex_RdTmp && type == Ity_I64) {
        /* handle the normal case */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                value);
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_RdTmp && type == Ity_F64) {
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                make_expr(sb, Ity_I64, unop(Iop_ReinterpF64asI64,
                        value)));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_RdTmp && tmp_needs_widen(type)) {
        /* the operation needs to be widened */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                make_expr(sb, Ity_I64, unop(widen_operation(sb, value),
                        value)));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_Const && const_needs_widen(value)) {
        /* the operation needs to be widened */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                widen_const(value));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (type == Ity_V128 || type == Ity_V256 ) {
        handle_wide_expr(sb, Iend_LE, daddr, value, guard, dsize);
    } else {
        VG_(umsg)("Unable to trace store - unsupported type of store 0x%x 0x%x\n",
                  value->tag, type);
    }
}

/**
* \brief Add a guarded write event for DWORD CAS. (BUGGED!)
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] dsize The size of the operation.
* \param[in] guard The guard expression.
* \param[in] value The expression with the value of the operation.
*/
static void
add_event_dw_guarded_dword(IRSB *sb, IRAtom *daddr, Int dsize, IRAtom *guard,
        IRAtom *value1, IRAtom *value2)
{
    static Bool reported = False;
    tl_assert(isIRAtom(daddr));
    tl_assert(isIRAtom(value1));
    tl_assert(isIRAtom(value2));
    tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

    const HChar *helperName = "trace_pmem_store_dword";
    void *helperAddr = trace_pmem_store_dword;
    IRExpr **argv;
    IRDirty *di;
    IRType type1 = typeOfIRExpr(sb->tyenv, value1);
    IRType type2 = typeOfIRExpr(sb->tyenv, value2);
    tl_assert(value1->tag && value2->tag);
    tl_assert(type1 == type2);
    tl_assert2(type1 == Ity_I64, "Only support for CMPXCHG16B is supported for DWord CAS; needs 64-bit integer!"); 
    tl_assert(value1->tag == value2->tag);

    if (value1->tag == Iex_RdTmp && type1 == Ity_I64) {
        /* handle the normal case */
        argv = mkIRExprVec_3(daddr, const_needs_widen(value1) ? widen_const(value1) : value1, const_needs_widen(value2) ? widen_const(value2) : value2);
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        // addStmtToIRSB(sb, IRStmt_Dirty(di));
        if (!reported) {
            VG_(emit)("Currently CMPXCHG16B is not supported for tracing!\n");
            reported = True;
        }        
    } else {
        tl_assert2(False, "Unable to trace store - unsupported type of store 0x%x:0x%x 0x%x\n",
                  value1->tag, value2->tag, type1);
    }
}

/**
* \brief Add an ordinary write event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] dsize The size of the operation.
* \param[in] value The expression with the value of the operation.
*/
static void
add_event_dw(IRSB *sb, IRAtom *daddr, Int dsize, IRAtom *value)
{
    add_event_dw_guarded(sb, daddr, dsize, NULL, value);
}


static void
_do_fence(void)
{   
    if (VG_(OSetGen_Size)(pmem.pmat_writeback_buffer_entries) == 0) {
        return;
    }
    ThreadId tid = VG_(get_running_tid)();
    XArray *arr = VG_(newXA)(VG_(malloc), "pmat_wb_fence", VG_(free), sizeof(struct pmat_writeback_buffer_entry));  
    VG_(OSetGen_ResetIter)(pmem.pmat_writeback_buffer_entries);
    struct pmat_writeback_buffer_entry *wbentry;
    while ( (wbentry = VG_(OSetGen_Next)(pmem.pmat_writeback_buffer_entries)) ) {
        if (wbentry->tid == tid) {
            VG_(addToXA)(arr, &wbentry); 
        }
    }
    Word nEntries = VG_(sizeXA)(arr);
    //VG_(emit)("Fencing %u entries for tid %lu\n", nEntries, tid);
    for (int i = 0; i < nEntries; i++) {
        wbentry = *(struct pmat_writeback_buffer_entry **) VG_(indexXA)(arr, i);
        write_to_file(wbentry);
        VG_(OSetGen_Remove)(pmem.pmat_writeback_buffer_entries, wbentry);
        VG_(free)(wbentry->entry);
        VG_(OSetGen_FreeNode)(pmem.pmat_writeback_buffer_entries, wbentry);
    }
    VG_(deleteXA)(arr);
}

/**
* \brief Fence operation.
*
* Ensures that cache lines that have been flushed but not yet written back
* are written back for the current thread. A point for crash simulation is
* injected before and then after the fence operation.
*/
static void
do_fence(void)
{
    _do_fence();
}

static void do_writeback(struct pmat_cache_entry *entry, Bool explicit) {    
    eviction_remove(TRIM_CACHELINE(entry->addr));
    ThreadId tid;
    // Flush was initiated by current thread...
    if (explicit) {
        tid = VG_(get_running_tid)();
    } else {
        // Was evicted; only a `fence` from original thread that last stored matters
        tid = entry->tid;
    }
    struct pmat_registered_file file = {0};
    file.addr = entry->addr; 
    struct pmat_registered_file *realFile = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
    // TODO: May want to move this behind some compile-time preprocessor directive
    if (!realFile) {
        VG_(emit)("Could not find descriptor for 0x%lx\n", file.addr);
        VG_(OSetGen_ResetIter)(pmem.pmat_registered_files);
        struct pmat_registered_file *tmp;
        while ((tmp = VG_(OSetGen_Next)(pmem.pmat_registered_files))) {
            VG_(emit)("File Found: (%lx, 0x%lx, 0x%lx)\n", tmp->descr, tmp->addr, tmp->size);
        }
    }
    tl_assert(realFile && "Unable to find descriptor associated with an address!");
    //VG_(emit)("Parent-Flush: (0x%lx, 0x%lx)\n", realFile->descr, entry->addr);
    
    // See if this entry already exists
    struct pmat_writeback_buffer_entry wblookup;
    wblookup.entry = entry;
    struct pmat_writeback_buffer_entry *exist = VG_(OSetGen_Lookup)(pmem.pmat_writeback_buffer_entries, &wblookup);
    if (exist) {
       write_to_file(exist);
       VG_(OSetGen_Remove)(pmem.pmat_writeback_buffer_entries, exist);
       VG_(free)(exist->entry);
       VG_(OSetGen_FreeNode)(pmem.pmat_writeback_buffer_entries, exist);
    }

    // Store Buffer
    struct pmat_writeback_buffer_entry *wbentry = VG_(OSetGen_AllocNode)(pmem.pmat_writeback_buffer_entries, (SizeT) sizeof(struct pmat_writeback_buffer_entry));
    wbentry->entry = entry;
    wbentry->tid = tid;
    if (explicit) {
        wbentry->locOfFlush = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);
    } else {
        wbentry->locOfFlush = NULL;
    }
    VG_(OSetGen_Insert)(pmem.pmat_writeback_buffer_entries, wbentry);
    if (VG_(OSetGen_Size)(pmem.pmat_writeback_buffer_entries) > pmem.pmat_num_wb_entries) {
        XArray *arr = VG_(newXA)(VG_(malloc), "pmat_wb_eviction", VG_(free), sizeof(struct pmat_writeback_buffer_entry));  
        VG_(OSetGen_ResetIter)(pmem.pmat_writeback_buffer_entries);
        struct pmat_writeback_buffer_entry *tmp;
        while ( (tmp = VG_(OSetGen_Next)(pmem.pmat_writeback_buffer_entries)) ) {
            if ((get_random() % 100) <= pmem.pmat_eviction_prob * 100) {
                VG_(addToXA)(arr, &tmp); 
            }
        }
        Word nEntries = VG_(sizeXA)(arr);
        for (int i = 0; i < nEntries; i++) {
            wbentry = *(struct pmat_writeback_buffer_entry **) VG_(indexXA)(arr, i);
            write_to_file(wbentry);
            VG_(OSetGen_Remove)(pmem.pmat_writeback_buffer_entries, wbentry);
            VG_(free)(wbentry->entry);
            VG_(OSetGen_FreeNode)(pmem.pmat_writeback_buffer_entries, wbentry);
        }
        VG_(deleteXA)(arr);
    }
}

/**
* \brief Register a flush.
*
* Marks dirty stores as flushed. The proper state transitions are
* DIRTY->FLUSHED->FENCED->COMMITTED->CLEAN. The CLEAN state is not registered,
* the store is removed from the set.
*
* \param[in] base The base address of the flush.
* \param[in] size The size of the flush in bytes.
*/
static void
do_flush(UWord base, UWord size) {
    // If the cache line has not been written back, write it into that cache-line.
    struct pmat_cache_entry *exists = eviction_lookup(TRIM_CACHELINE(base));
    if (exists) {
        do_writeback(exists, True);
    }
}

/**
 * \brief Register runtime flush.
 * \param addr[in] addr The expression with the address of the operation.
 */
static VG_REGPARM(1) void
trace_pmem_flush(Addr addr)
{
    /* use native cache size for flush */
    do_flush(addr, PMAT_CACHELINE_SIZE);
}

/*
    Handles CLFLUSH which is a flush+fence combination; this will ensure that
    a simulation of a crash does not occur in between the flush and the fence,
    eliminating any cases of false positives of 'leaked cache lines'. This will
    call flush and fence in a way that no simulated crash occurs in between them.
*/
static VG_REGPARM(1) void
trace_pmem_flush_fence(Addr addr) 
{
    do_flush(addr, PMAT_CACHELINE_SIZE);
    _do_fence();
}

/**
* \brief Add an ordinary flush event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] isFence The flush is a CLFLUSH and hence needs to be treated as a fence as well
*/
static void
add_flush_event(IRSB *sb, IRAtom *daddr, Bool isFence)
{
    tl_assert(isIRAtom(daddr));

    if (!isFence) {
        const HChar *helperName = "trace_pmem_flush";
        void *helperAddr = trace_pmem_flush;
        IRExpr **argv;
        IRDirty *di;

        argv = mkIRExprVec_1(daddr);
        di = unsafeIRDirty_0_N(/*regparms*/1, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);

        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else {
        const HChar *helperName = "trace_pmem_flush_fence";
        void *helperAddr = trace_pmem_flush_fence;
        IRExpr **argv;
        IRDirty *di;

        argv = mkIRExprVec_1(daddr);
        di = unsafeIRDirty_0_N(/*regparms*/1, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);

        addStmtToIRSB(sb, IRStmt_Dirty(di));
    }
}

/**
* \brief Add an event without any parameters.
* \param[in,out] sb The IR superblock to which the expression belongs.
*/
static void
add_simple_event(IRSB *sb, void *helperAddr, const HChar *helperName)
{
    IRDirty *di;

    di = unsafeIRDirty_0_N(/*regparms*/0, helperName,
            VG_(fnptr_to_fnentry)(helperAddr), mkIRExprVec_0());

    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

/**
* \brief Print gdb monitor commands.
*/
static void
print_monitor_help(void)
{
    VG_(gdb_printf)
            ("\n"
            "pmat gdb monitor commands:\n"
            "  print_stats\n"
            "        prints the summary\n"
            "  print_pmem_regions \n"
            "        prints the registered persistent memory regions\n"
            "\n");
}

/**
* \brief Gdb monitor command handler.
* \param[in] tid Id of the calling thread.
* \param[in] req Command request string.
* \return True if command is recognized, true otherwise.
*/
static Bool handle_gdb_monitor_command(ThreadId tid, HChar *req)
{
    HChar* wcmd;
    HChar s[VG_(strlen(req)) + 1]; /* copy for strtok_r */
    HChar *ssaveptr;

    VG_(strcpy) (s, req);

    wcmd = VG_(strtok_r) (s, " ", &ssaveptr);
    switch (VG_(keyword_id)
            ("help print_stats print_pmem_regions",
                    wcmd, kwd_report_duplicated_matches)) {
        case -2: /* multiple matches */
            return True;

        case -1: /* not found */
            return False;

        case  0: /* help */
            print_monitor_help();
            return True;

        default:
            tl_assert(0);
            return False;
    }
}

/**
* \brief The main instrumentation function - the heart of the tool.
*
* The translated client code is passed into this function, where appropriate
* instrumentation is made. All uninteresting operations are copied straight
* to the returned IRSB. The only interesting operations are stores, which are
* instrumented for further analysis.
* \param[in] closure Valgrind closure - unused.
* \param[in] bb The IR superblock provided by the core.
* \param[in] layout Vex quest layout - unused.
* \param[in] vge Vex quest extents - unused.
* \param[in] archinfo_host Vex architecture info - unused.
* \param[in] gWordTy Guest word type.
* \param[in] hWordTy Host word type.
* \return The modified IR superblock.
*/
static IRSB*
pmat_instrument(VgCallbackClosure *closure,
        IRSB *bb,
        const VexGuestLayout *layout,
        const VexGuestExtents *vge,
        const VexArchInfo *archinfo_host,
        IRType gWordTy, IRType hWordTy)
{
    Int i;
    IRSB *sbOut;
    IRTypeEnv *tyenv = bb->tyenv;

    if (gWordTy != hWordTy) {
        /* We don't currently support this case. */
        VG_(tool_panic)("host/guest word size mismatch");
    }

    /* Set up SB */
    sbOut = deepCopyIRSBExceptStmts(bb);

    /* Copy verbatim any IR preamble preceding the first IMark */
    i = 0;
    while (i < bb->stmts_used && bb->stmts[i]->tag != Ist_IMark) {
        addStmtToIRSB(sbOut, bb->stmts[i]);
        ++i;
    }

    /* Count this superblock. */
    IRDirty *di = unsafeIRDirty_0_N( 0, "add_one_SB_entered",
            VG_(fnptr_to_fnentry)(&add_one_SB_entered), mkIRExprVec_0());
    addStmtToIRSB(sbOut, IRStmt_Dirty(di));

    for (/*use current i*/; i < bb->stmts_used; i++) {
        IRStmt *st = bb->stmts[i];
        if (!st || st->tag == Ist_NoOp)
            continue;

        switch (st->tag) {
            case Ist_IMark:
            case Ist_AbiHint:
            case Ist_Put:
            case Ist_PutI:
            case Ist_LoadG:
            case Ist_WrTmp:
            case Ist_Exit:
            case Ist_Dirty:
                /* for now we are not interested in any of the above */
                addStmtToIRSB(sbOut, st);
                break;                

            case Ist_Flush: {
                //add_simple_event(sbOut, beforeFlush, "beforeFlush");
                addStmtToIRSB(sbOut, st);
                IRExpr *addr = st->Ist.Flush.addr;
                IRType type = typeOfIRExpr(tyenv, addr);
                tl_assert(type != Ity_INVALID);
                add_flush_event(sbOut, st->Ist.Flush.addr, st->Ist.Flush.fk == Ifk_flush);
                break;
            }

            case Ist_MBE: {
                addStmtToIRSB(sbOut, st);                
                switch (st->Ist.MBE.event) {
                    case Imbe_Fence:
                    case Imbe_SFence:
                        add_simple_event(sbOut, do_fence, "do_fence");
                        break;
                    default:
                        break;
                }
                break;
            }

            case Ist_Store: {
                addStmtToIRSB(sbOut, st);
                IRExpr *data = st->Ist.Store.data;
                IRType type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                add_event_dw(sbOut, st->Ist.Store.addr, sizeofIRType(type),
                        data);
                break;
            }

            case Ist_StoreG: {
                addStmtToIRSB(sbOut, st);
                IRStoreG *sg = st->Ist.StoreG.details;
                IRExpr *data = sg->data;
                IRType type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                add_event_dw_guarded(sbOut, sg->addr, sizeofIRType(type),
                        sg->guard, data);
                break;
            }

            case Ist_CAS: {
                Int dataSize;
                IRType dataTy;
                IRCAS *cas = st->Ist.CAS.details;
                tl_assert(cas->addr != NULL);
                tl_assert(cas->dataLo != NULL);
                dataTy = typeOfIRExpr(tyenv, cas->dataLo);
                dataSize = sizeofIRType(dataTy);
                /* has to be done before registering the guard */
                addStmtToIRSB(sbOut, st);
                /* the guard statement on the CAS */
                IROp opCasCmpEQ;
                IROp opOr;
                IROp opXor;
                IRAtom *zero = NULL;
                IRType loType = typeOfIRExpr(tyenv, cas->expdLo);
                switch (loType) {
                    case Ity_I8:
                        opCasCmpEQ = Iop_CasCmpEQ8;
                        opOr = Iop_Or8;
                        opXor = Iop_Xor8;
                        break;
                    case Ity_I16:
                        opCasCmpEQ = Iop_CasCmpEQ16;
                        opOr = Iop_Or16;
                        opXor = Iop_Xor16;
                        break;
                    case Ity_I32:
                        opCasCmpEQ = Iop_CasCmpEQ32;
                        opOr = Iop_Or32;
                        opXor = Iop_Xor32;
                        break;
                    case Ity_I64:
                        opCasCmpEQ = Iop_CasCmpEQ64;
                        opOr = Iop_Or64;
                        opXor = Iop_Xor64;
                        break;
                    default:
                        tl_assert(0);
                }

                if (cas->dataHi != NULL) {
                    IRAtom *xHi = NULL;
                    IRAtom *xLo = NULL;
                    IRAtom *xHL = NULL;
                    xHi = make_expr(sbOut, loType, binop(opXor, cas->expdHi,
                            mkexpr(cas->oldHi)));
                    xLo = make_expr(sbOut, loType, binop(opXor, cas->expdLo,
                            mkexpr(cas->oldLo)));
                    xHL = make_expr(sbOut, loType, binop(opOr, xHi, xLo));
                    // Guard = (expectedHi == srcHi && expectedLo == srcLo)
                    IRAtom *guard = make_expr(sbOut, Ity_I1,
                            binop(opCasCmpEQ, xHL, zero));
                    add_event_dw_guarded_dword(sbOut, cas->addr, dataSize, guard,
                            cas->dataLo, cas->dataHi);
                } else {
                    IRAtom *guard = make_expr(sbOut, Ity_I1, binop(opCasCmpEQ,
                            cas->expdLo, mkexpr(cas->oldLo)));

                    add_event_dw_guarded(sbOut, cas->addr, dataSize, guard,
                            cas->dataLo);
                }
                break;
            }

            case Ist_LLSC: {
                addStmtToIRSB(sbOut, st);
                IRType dataTy;
                if (st->Ist.LLSC.storedata != NULL) {
                    dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
                    add_event_dw(sbOut, st->Ist.LLSC.addr, sizeofIRType
                            (dataTy), st->Ist.LLSC.storedata);
                }
                break;
            }

            default:
                ppIRStmt(st);
                tl_assert(0);
        }
    }

    return sbOut;
}

/**
* \brief Client mechanism handler.
* \param[in] tid Id of the calling thread.
* \param[in] arg Arguments passed in the request, 0-th is the request name.
* \param[in,out] ret Return value passed to the client.
* \return True if the request has been handled, false otherwise.
*/
static Bool
pmat_handle_client_request(ThreadId tid, UWord *arg, UWord *ret )
{
    if (!VG_IS_TOOL_USERREQ('P', 'C', arg[0])
            && VG_USERREQ__PMC_PMAT_FORCE_SIMULATE_CRASH != arg[0]
            && VG_USERREQ__PMC_DO_FLUSH != arg[0]
            && VG_USERREQ__PMC_DO_FENCE != arg[0]
            && VG_USERREQ__PMC_WRITE_STATS != arg[0]
            && VG_USERREQ__GDB_MONITOR_COMMAND != arg[0]
            && VG_USERREQ__PMC_PMAT_REGISTER != arg[0]
            && VG_USERREQ__PMC_PMAT_UNREGISTER_BY_ADDR != arg[0]
            && VG_USERREQ__PMC_PMAT_UNREGISTER_BY_NAME != arg[0]
            && VG_USERREQ__PMC_PMAT_CRASH_ENABLE != arg[0]
            && VG_USERREQ__PMC_PMAT_CRASH_DISABLE != arg[0]
            && VG_USERREQ__PMC_PMAT_TRANSIENT != arg[0]
            && VG_USERREQ__PMC_PMAT_IS_PERSIST != arg[0]
            && VG_USERREQ__PMC_PMAT_PERSIST_ORDER != arg[0]
            && VG_USERREQ__PMC_RESERVED1 != arg[0]
            && VG_USERREQ__PMC_RESERVED2 != arg[0]
            && VG_USERREQ__PMC_RESERVED3 != arg[0]
            && VG_USERREQ__PMC_RESERVED4 != arg[0]
            && VG_USERREQ__PMC_RESERVED5 != arg[0]
            && VG_USERREQ__PMC_RESERVED6 != arg[0]
            && VG_USERREQ__PMC_RESERVED7 != arg[0]
            && VG_USERREQ__PMC_RESERVED8 != arg[0]
            )
        return False;

    switch (arg[0]) {
        case VG_USERREQ__PMC_PMAT_PERSIST_ORDER: {
            // TODO: Fill out and create some data structure to hold everything.
        }
        // Check both simulated CPU Cache and whether or not write-back reordering buffer contains cache-line(s) for [addr, addr + sz)
        // TODO: Handle cases where we cross multiple cache-lines
        case VG_USERREQ__PMC_PMAT_IS_PERSIST: {
            VG_(emit)("PMAT_IS_PERSIST is not implemented!");
            int *retaddr = arg[3];
            *retaddr = -1;
            break;
        }
        // Add to table of addresses to ignore.
        case VG_USERREQ__PMC_PMAT_TRANSIENT: {
            // Check if the address is already included in some persistent
            // memory region that is currently enabled.
            if (VG_(OSetGen_Size)(pmem.pmat_registered_files) > 0) {                
                struct pmat_registered_file file = {0};
                file.addr = arg[1];
                Bool found = !!VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
                if (!found) {
                    break;
                }
            }
            struct pmat_transient_entry *entry = VG_(OSetGen_AllocNode)(pmem.pmat_transient_addresses, sizeof(*entry));
            entry->addr = arg[1];
            entry->size = arg[2];

            // Check if exists...
            if (!VG_(OSetGen_Contains)(pmem.pmat_transient_addresses, entry)) {
                VG_(OSetGen_Insert)(pmem.pmat_transient_addresses, entry);
            }
            break;
        }
        case VG_USERREQ__PMC_PMAT_CRASH_ENABLE: {
            pmem.pmat_should_verify = True;
            break;
        }
        case VG_USERREQ__PMC_PMAT_CRASH_DISABLE: {
            pmem.pmat_should_verify = False;
            break;
        }
        case VG_USERREQ__PMC_PMAT_REGISTER_WITH_FN: 
        case VG_USERREQ__PMC_PMAT_REGISTER: {
            // TODO: Need to actually appropriately handle this under new model;
            // Should now only take an address; verification program should have
            // specific arguments and should be specified by command-line.
            HChar *_name = (char *) arg[1];
            Addr addr = arg[2];
            UWord size = arg[3];
            if (!_name) {
                VG_(fmsg)("First argument 'name' must _not_ be NULL!\n");
                return False;
            }
            if (TRIM_CACHELINE(addr) != addr) {
                VG_(fmsg)("Address 0x%lx is not aligned to cache line size of %lld!\n", addr, CACHELINE_SIZE);
                return False;
            }
            
            // Create copy of 'name' in case user passes in non-constant heap-allocated data
            HChar *name = VG_(malloc)("File Name Copy", VG_(strlen)(_name));
            tl_assert(name);
            VG_(strcpy)(name, _name);
            struct pmat_registered_file *file = VG_(OSetGen_AllocNode)(pmem.pmat_registered_files, (SizeT) sizeof(struct pmat_registered_file));
            tl_assert(file);
            file->addr = addr;
            file->size = size;
            file->name = name;
            if (arg[0] == VG_USERREQ__PMC_PMAT_REGISTER_WITH_FN) {
                file->verify_fn = arg[4];
            }
            SysRes res = VG_(open)(file->name, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_RDWR, 0666);
            if (sr_isError(res)) {
                VG_(emit)("Could not open file '%s'; errno: %ld\n", file->name, sr_Err(res));
                tl_assert(0);
            }
            file->descr = sr_Res(res);
            VG_(ftruncate)(file->descr, file->size);
            tl_assert(file->descr != (UWord) -1);

            // Copy over in-memory contents into shadow-heap. Since we know
            // that we have thread serialization thanks to Valgrind, we know
            // that the heap cannot be modified while we are making this copy.
            VG_(OSetGen_Insert)(pmem.pmat_registered_files, file);
            addr = VG_(mmap)((Addr) NULL, file->size, VKI_PROT_READ | VKI_PROT_WRITE,  VKI_MAP_SHARED, file->descr, 0);
            tl_assert2(addr != ((Addr) -1), "MMAP failed!");
            VG_(memcpy)((void *) addr, (void *) file->addr, file->size);
            file->mmap_addr = addr;
            break;
        }
        case VG_USERREQ__PMC_PMAT_UNREGISTER_BY_ADDR: {
            if (VG_(OSetGen_Size)(pmem.pmat_registered_files) > 0) {           
                struct pmat_registered_file file = {0};
                file.addr = arg[1];
                struct pmat_registered_file *found = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_addr);
                if (!found) {
                    break;
                }
                VG_(OSetGen_Remove)(pmem.pmat_registered_files, found);
                VG_(OSetGen_FreeNode)(pmem.pmat_registered_files, found);
            }
            break;
        }
        case VG_USERREQ__PMC_PMAT_UNREGISTER_BY_NAME: {
            if (VG_(OSetGen_Size)(pmem.pmat_registered_files) > 0) {           
                struct pmat_registered_file file = {0};
                file.name = (char *) arg[1];
                struct pmat_registered_file *found = VG_(OSetGen_LookupWithCmp)(pmem.pmat_registered_files, &file, (OSetCmp_t) find_file_by_name);
                if (!found) {
                    break;
                }
                VG_(OSetGen_Remove)(pmem.pmat_registered_files, found);
                VG_(OSetGen_FreeNode)(pmem.pmat_registered_files, found);
            }
            break;
        }
        case VG_USERREQ__PMC_PMAT_FORCE_SIMULATE_CRASH: {
            simulate_crash();
            break;
        }

        case VG_USERREQ__PMC_DO_FLUSH: {
            do_flush(arg[1], arg[2]);
            break;
        }

        case VG_USERREQ__PMC_DO_FENCE: {
            do_fence();
            break;
        }

        case VG_USERREQ__GDB_MONITOR_COMMAND: {
            Bool handled = handle_gdb_monitor_command (tid, (HChar*)arg[1]);
            if (handled)
                *ret = 0;
            else
                *ret = 1;
            return handled;
        }

        case VG_USERREQ__PMC_RESERVED1: {
            /* deprecated - do not use */
            break;
        }

        case VG_USERREQ__PMC_RESERVED2:
        case VG_USERREQ__PMC_RESERVED3:
        case VG_USERREQ__PMC_RESERVED4:
        case VG_USERREQ__PMC_RESERVED5:
        case VG_USERREQ__PMC_RESERVED6:
        case VG_USERREQ__PMC_RESERVED7:
        case VG_USERREQ__PMC_RESERVED8: {
            VG_(message)(
                    Vg_UserMsg,
                    "Warning: deprecated pmat client request code 0x%llx\n",
                    (ULong)arg[0]
            );
            return False;
        }

        default:
            VG_(message)(
                    Vg_UserMsg,
                    "Warning: unknown pmat client request code 0x%llx\n",
                    (ULong)arg[0]
            );
            return False;
    }
    return True;
}

/**
* \brief Handle tool command line arguments.
* \param[in] arg Tool command line arguments.
* \return True if the parameter is recognized, false otherwise.
*/
static Bool
pmat_process_cmd_line_option(const HChar *arg)
{
    if VG_STR_CLO(arg, "--verifier", pmem.pmat_verifier) {}
    else if VG_DBL_CLO(arg, "--eviction-probability", pmem.pmat_eviction_prob) {}
    else if VG_DBL_CLO(arg, "--crash-probability", pmem.pmat_crash_prob) {}
    else if VG_INT_CLO(arg, "--num-cache-entries", pmem.pmat_num_cache_entries) {}
    else if VG_INT_CLO(arg, "--num-wb-entries", pmem.pmat_num_wb_entries) {}
    else if VG_INT_CLO(arg, "--rng-seed", pmem.pmat_rng_seed) {}
    else if VG_BOOL_CLO(arg, "--preserve-bin-on-error", pmem.pmat_preserve_bin_on_error) {}
    else if VG_BOOL_CLO(arg, "--aggregate-dump-only", pmem.pmat_aggregate_dump_only) {}
    else if VG_BOOL_CLO(arg, "--terminate-on-error", pmem.pmat_terminate_on_error) {}
    else if VG_STR_CLO(arg, "--eviction-policy", pmem.pmat_eviction_policy_str) {}
    else return False;

    return True;
}


/**
* \brief Post command line options initialization.
*/
static void
pmat_post_clo_init(void)
{
    pmem.pmat_writeback_buffer_entries = VG_(OSetGen_Create_With_Pool)(0, cmp_pmat_write_buffer_entries, VG_(malloc), "pmat.main.cpci.-2", VG_(free),
            MAX(100, pmem.pmat_num_wb_entries), (SizeT) sizeof(struct pmat_writeback_buffer_entry));
    pmem.pmat_transient_addresses = VG_(OSetGen_Create)(0, cmp_pmat_transient_entries, VG_(malloc), "pmi.main.cpci.-3", VG_(free));
    if (pmem.pmat_aggregate_dump_only) {
        pmem.pmat_aggregate_cache_dump = VG_(OSetGen_Create)(0, cmp_exe_context_pointers, VG_(malloc), "pmat.main.cpci.-4", VG_(free));
        pmem.pmat_aggregate_flushed_dump = VG_(OSetGen_Create)(0, cmp_flush_locations, VG_(malloc), "pmat.main.cpci.-5", VG_(free));
    }
    pmem.pmat_should_verify = True;
    // Parent compares based on 'Addr' so that it can find the descr associated with the address.
    pmem.pmat_registered_files = VG_(OSetGen_Create)(0, cmp_pmat_registered_files1, VG_(malloc), "pmat.main.cpci.-1", VG_(free));
    VG_(emit)(
        "Verifier = %s\n"
        "Eviction Rate = %.0f%%\n"
        "Crash Rate = %.0f%%\n"
        "Simulated Processor Cache Capacity = %ld Entries\n"
        "Write-Back Reordering Buffer Capacity = %ld Entries\n"
        "RNG Seed = %x(%u)\n"
        "Eviction Policy = %s\n",
        pmem.pmat_verifier,
        pmem.pmat_eviction_prob * 100,
        pmem.pmat_crash_prob * 100,
        pmem.pmat_num_cache_entries,
        pmem.pmat_num_wb_entries,
        pmem.pmat_rng_seed, pmem.pmat_rng_seed,
        pmem.pmat_eviction_policy_str
    );

    if (VG_(strncasecmp)(pmem.pmat_eviction_policy_str, "RR", 2) == 0) {
        pmem.pmat_eviction_policy.arg = pmat_create_rr();
        pmem.pmat_eviction_policy.insert = pmat_rr_cache_insert;
        pmem.pmat_eviction_policy.remove = pmat_rr_cache_remove;
        pmem.pmat_eviction_policy.evict = pmat_rr_cache_evict;
        pmem.pmat_eviction_policy.lookup = pmat_rr_cache_lookup;
        pmem.pmat_eviction_policy.size = pmat_rr_cache_size;
        pmem.pmat_eviction_policy.to_array = pmat_rr_cache_to_array;
    } else if (VG_(strncasecmp)(pmem.pmat_eviction_policy_str, "LRU", 3) == 0) {
        pmem.pmat_eviction_policy.arg = pmat_create_lru();
        pmem.pmat_eviction_policy.insert = pmat_lru_cache_insert;
        pmem.pmat_eviction_policy.remove = pmat_lru_cache_remove;
        pmem.pmat_eviction_policy.evict = pmat_lru_cache_evict;
        pmem.pmat_eviction_policy.lookup = pmat_lru_cache_lookup;
        pmem.pmat_eviction_policy.size = pmat_lru_cache_size;
        pmem.pmat_eviction_policy.to_array = pmat_lru_cache_to_array;
    } else {
        VG_(emit)("[ERROR] Bad eviction policy provided: '%s'; Require 'RR' or 'LRU' (not case sensitive)!\n", pmem.pmat_eviction_policy_str);
        VG_(exit)(1);
    }
}

/**
* \brief Print usage.
*/
static void
pmat_print_usage(void)
{
    VG_(emit)(
            "    --verifier=<path/to/exec>         verifier to call when simulating crash\n"
            "                                      default [no verification]\n"
            "    --eviction-probability=p          The probability of cache-line eviction\n"
            "                                      default [0.5]\n"
            "    --crash-probability=p             The probability of crash simulation\n"
            "                                      default [0.01]\n"
            "    --num-cache-entries=N             The maximum number of entries in the cache\n"
            "                                      default [1048576]\n"
            "    --num-wb-entries=N                The maximum number of entries in the write-back reordering buffer\n"
            "                                      default [131072]\n"
            "    --rng-seed=N                      The value of RNG seed used when simulating a crash or evicting entries\n"
            "                                      default [/dev/urandom]\n"
            "    --preserve-bin-on-error=yes|no    Preserve the copy of the shadow region alongside .stderr, .stdout, and .dump files.\n"
            "                                      default [no]\n"
            "    --aggregate-dump-only=yes|no      Aggregate and coalesce .dump files into a single `aggregated.dump` file.\n"
            "                                      default [no]\n"
            "    --terminate-on-error=yes|no       Terminates the program on the first error occurred, rather than continuing execution.\n"
            "                                      default [no]\n"
            "    --eviction-policy=RR|LRU          Determines the eviction policy to be used.\n"
            "                                      default [RR] (random-replacement)"
    );
}

/**
* \brief Print debug usage.
*/
static void
pmat_print_debug_usage(void)
{
    VG_(emit)(
            "    (none)\n"
    );
}

static Double sqrt(Double number)
{
    double error = 0.00001; //define the precision of your result
    double s = number;

    while ((s - number / s) > error) //loop until precision satisfied 
    {
        s = (s + number / s) / 2;
    }
    return s;
}

static void scientificNotation(Double d, Double *norm, Word *exp) {
    *norm = d;
    *exp = 0;
    if (*norm) {
        while (*norm >= 10.0) {
            *norm /= 10.0;
            exp[0]++;
        }
        while (*norm < 1.0) {
            *norm *= 10.0;
            exp[0]--;
        }
    }
}

/**
 * \brief Function called on program exit.
 */
static void
pmat_fini(Int exitcode)
{
    if (pmem.pmat_verifier) {
        print_store_stats();
        if (pmem.num_verifications) {
            Double mean, var, mins, maxs, stds;
            Word mean_norm, var_norm, mins_norm, maxs_norm, stds_norm;
            get_stats(&mean, &var);
            mins = pmem.min_verification_time;
            maxs = pmem.max_verification_time;
            stds = sqrt(var);
            scientificNotation(mean, &mean, &mean_norm);
            scientificNotation(var, &var, &var_norm);
            scientificNotation(mins, &mins, &mins_norm);
            scientificNotation(maxs, &maxs, &maxs_norm);
            scientificNotation(stds, &stds, &stds_norm);
            
            VG_(emit)("Verification Function Stats (seconds):\n\tMinimum:%lf%s%ld\n\tMaximum:%lf%s%ld\n\tMean:%lf%s%ld\n\tVariance:%lf%s%ld\n",
                mins, mins_norm ? "e" : "", mins_norm, maxs, maxs_norm ? "e" : "", maxs_norm, mean, mean_norm ? "e" : "", mean_norm, var, var_norm ? "e" : "", var_norm);
            
            if (pmem.pmat_aggregate_dump_only) dump_aggregate_to_file();
        }
    }
}

/**
* \brief Pre command line options initialization.
*/
static void
pmat_pre_clo_init(void)
{
    VG_(details_name)("PMAT");
    VG_(details_version)("0.1");
    VG_(details_description)("Persistent Memory Analysis Tool");
    VG_(details_copyright_author)("University of Rochester");
    VG_(details_bug_reports_to)("louis.jenkins@rochester.edu");

    VG_(details_avg_translation_sizeB)(275);

    VG_(basic_tool_funcs)(pmat_post_clo_init, pmat_instrument, pmat_fini);

    VG_(needs_command_line_options)(pmat_process_cmd_line_option,
            pmat_print_usage, pmat_print_debug_usage);

    VG_(needs_client_requests)(pmat_handle_client_request);

    /* support only 64 bit architectures */
    tl_assert(VG_WORDSIZE == 8);
    tl_assert(sizeof(void*) == 8);
    tl_assert(sizeof(Addr) == 8);
    tl_assert(sizeof(UWord) == 8);
    tl_assert(sizeof(Word) == 8);

    pmem.num_verifications = 0;
    pmem.num_bad_verifications = 0;
    pmem.min_verification_time = 0;
    pmem.max_verification_time = 0;
    pmem.ssd_verification_time = 0;
    pmem.average_verification_time = 0;
    pmem.pmat_num_cache_entries = 1024 * 1024;
    pmem.pmat_num_wb_entries = 128 * 1024;
    pmem.pmat_crash_prob = 0.01;
    pmem.pmat_eviction_prob = 0.1;
    pmem.pmat_rng_seed = get_urandom();
    pmem.pmat_preserve_bin_on_error = False;
    pmem.pmat_aggregate_dump_only = False;
    pmem.pmat_terminate_on_error = False;
    pmem.pmat_eviction_policy_str = "RR";
}

VG_DETERMINE_INTERFACE_VERSION(pmat_pre_clo_init)
