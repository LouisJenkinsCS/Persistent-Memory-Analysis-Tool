/**
 * Utilities that should be utilized by tests and microbenchmarks
 */

#ifndef PMAT_UTILS
#define PMAT_UTILS

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>

// Configurable Constants
#ifndef MMAP_ADDRESS
#define MMAP_ADDRESS (void *) ((0x12345678ULL & ~(4096 - 1)) + 4096)
#endif

#ifndef MAX_NUM_THREADS
#define MAX_NUM_THREADS 16
#endif

// Convenience Functions
#define CLFLUSH(addr) asm volatile ("clflush (%0)" :: "r"(addr))
#define CLFLUSHOPT(addr) asm volatile ("clflushopt (%0)" :: "r"(addr));
#define SFENCE() asm volatile("sfence" : : : "memory")


#define CREATE_HEAP(name, size) ({ \
    int __fd = open(name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH); \
    int __retval __attribute__ ((unused)) = ftruncate(__fd, size); \
    void *__arr = mmap(MMAP_ADDRESS, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, __fd, 0); \
    __arr; \
})

#define OPEN_HEAP(name, accessFlags, sizePtr) ({ \
    int __fd = open(name, accessFlags); \
    struct stat __sb; \
    (void) fstat(__fd, &__sb); \
    int __sz = __sb.st_size; \
    void *__arr = mmap(MMAP_ADDRESS, __sz, (accessFlags & O_RDWR) ? PROT_READ | PROT_WRITE : (accessFlags & O_WRONLY) ? PROT_WRITE : PROT_READ, MAP_SHARED | MAP_FIXED, __fd, 0); \
    *(sizePtr) = __sz; \
    __arr; \
})

// Timer functions
static size_t timer_msec = 0;
static clock_t timer_before;

static void setTimeout(size_t msec) {
    timer_before = clock();
    timer_msec = msec;
}

static bool hasTimedOut() {
    clock_t difference = clock() - timer_before;
    size_t msec = difference * 1000 / CLOCKS_PER_SEC;
    return timer_msec <= msec;
}
#endif