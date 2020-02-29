#include "DurableQueue.hpp"

int main(void) {
    auto dq = DurableQueue::alloc<int, -1>(new uint8_t[1024], 1024);
    #pragma omp parallel for
    for (int i = 0; i < 10; i++) assert(dq->enqueue(1));
    std::cout << dq << std::endl;
}