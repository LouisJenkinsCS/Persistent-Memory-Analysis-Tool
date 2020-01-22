#ifndef PMAT_INCLUDE_H
#define PMAT_INCLUDE_H

/*
    Dependency Graph used to declare ordering constraints between memory locations.
*/


struct pmat_transient_entry {
    Addr addr;
    ULong size;
};

struct pmat_cache_entry {
    // Bitmap to keep track of dirty bits
    ExeContext *lastPendingStore;
    ULong dirtyBits;
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
#define CACHELINE_SIZE 64ULL
#define TRIM_CACHELINE(addr) ((addr) &~ (CACHELINE_SIZE - 1ULL))
#define OFFSET_CACHELINE(addr) ((addr) % CACHELINE_SIZE)
#define NUM_CACHE_ENTRIES 1024ULL
#define NUM_WB_ENTRIES 64ULL

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

Word cmp_pmat_cache_entries(const void *key, const void *elem);

Word cmp_pmat_registered_files1(const void *key, const void *elem);

Word cmp_pmat_registered_files2(const void *key, const void *elem);

Word cmp_pmat_write_buffer_entries(const void *key, const void *elem);

Word cmp_pmat_transient_entries(const void *key, const void *elem);

#endif	/* PMAT_INCLUDE_H */

