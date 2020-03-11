#include "DurableQueue.hpp"
#include <valgrind/pmat.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../../utils.h"

#define DQ_NULL -1
#define DQ_EMPTY -2

int main(int argc, char *argv[]) {
	assert(argc >= 3);
    assert(strcmp(argv[1], "1") == 0);

    size_t sz;
    void *heap = OPEN_HEAP(argv[2], O_RDWR, &sz);
    assert(heap != (void *) -1);

    DurableQueue::DurableQueue<int, DQ_EMPTY, DQ_NULL> *dq = DurableQueue::recover<int, DQ_EMPTY, DQ_NULL>(reinterpret_cast<uint8_t*>(heap), sz);

    if (dq == NULL) {
        std::cerr << "DurableQueue::recover returned nullptr!" << std::endl;
        // Failure!
        return PMAT_VERIFICATION_FAILURE;
    } else {
        // Success!
        return 0;
    }
}
