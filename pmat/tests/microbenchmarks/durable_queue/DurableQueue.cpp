#include "DurableQueue.hpp"
#include <valgrind/pmat.h>
#include <cstdlib>
#include <omp.h>
#include <string>
#include <sstream>
#include <cassert>

#define DQ_NULL -1
#define DQ_EMPTY -2

template <class T, T NilT, T EmptyT>
void sanity_check(DurableQueue::DurableQueue<T, NilT, EmptyT> *dq, size_t numNodes) {
    // Do three rounds of insertions and removals...
    for (int i = 0; i < 3; i++) {
        int enqueueRedux = 0;
        #pragma omp parallel for reduction(+:enqueueRedux)
        // Hazard Pointers do not immediately reclaim, and so some slack is required...
        for (int i = 0; i < numNodes / 2; i++) {
            assert(dq->enqueue(i, omp_get_thread_num()));
            enqueueRedux += i;
        }
        
        int dequeueRedux = 0;
        #pragma omp parallel for reduction(+:dequeueRedux)
        for (int i = 0; i < numNodes / 2; i++) {
            int retval = dq->dequeue(omp_get_thread_num());
            assert(retval != DQ_EMPTY);
            dequeueRedux += retval;
        } 
        assert(enqueueRedux == dequeueRedux);
        dq->sanity();
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    long numNodes;
    std::istringstream(argv[1]) >> numNodes;
    size_t size = sizeof(DurableQueue::DurableQueue<int, DQ_EMPTY, DQ_NULL>) + numNodes * sizeof(DurableQueue::DurableQueueNode<int, DQ_NULL>);
    
    if (numNodes > DQ_MAX_THREADS * 4) {
        std::cerr << "[Warning] Hazard Pointers requires quite a bit of slack, so it is recommended that more nodes are added if this fails to complete..." << std::endl;
    }
    
    // Create my heap...
    uint8_t *buffer = reinterpret_cast<uint8_t*>(std::aligned_alloc(PMAT_CACHELINE_SIZE, size));
    assert(buffer != nullptr);

    auto dq = DurableQueue::alloc<int, DQ_EMPTY, DQ_NULL>(buffer, size);
    sanity_check(dq, numNodes);
}