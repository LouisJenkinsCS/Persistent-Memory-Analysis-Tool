#include "DurableQueue.hpp"
#include <omp.h>

#define DQ_NULL -1
#define DQ_EMPTY -2


int main(void) {
    auto dq = DurableQueue::alloc<int, DQ_EMPTY, DQ_NULL>(new uint8_t[1024], 1024);
    int redux = 0;
    #pragma omp parallel for reduction(+:redux)
    for (int i = 0; i < 10; i++) {
        assert(dq->enqueue(i));
        redux += i;
    }
    std::cout << "Enqueue Redux: " << redux << std::endl;
    std::cout << dq << std::endl;
    
    redux = 0;
    #pragma omp parallel for reduction(+:redux)
    for (int i = 0; i < 10; i++) {
        int retval = dq->dequeue(omp_get_thread_num());
        assert(retval != DQ_EMPTY);
        redux += retval;
    } 
    std::cout << "Dequeue Redux: " << redux << std::endl;
}