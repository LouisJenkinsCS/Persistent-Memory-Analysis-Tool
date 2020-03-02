#include "DurableQueue.hpp"
#include <valgrind/pmat.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DQ_NULL -1
#define DQ_EMPTY -2

int main(int argc, char *argv[]) {
	assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    int fd = open(argv[2], O_RDONLY);
    assert(fd != -1);
    struct stat sb;
    int retval = fstat(fd, &sb);
    assert(retval != -1);
    size_t sz = sb.st_size;
    void *heap = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(heap != (void *) -1);
    //assert(SIZE == sz);
    DurableQueue::DurableQueue<int, DQ_EMPTY, DQ_NULL> *dq = DurableQueue::recover<int, DQ_EMPTY, DQ_NULL>(reinterpret_cast<uint8_t*>(heap), sz);
    munmap(heap, sz);

    if (dq == NULL) {
        std::cerr << "DurableQueue::recover returned nullptr!" << std::endl;
        // Failure!
        return PMAT_VERIFICATION_FAILURE;
    } else {
        // Success!
        return 0;
    }
}
