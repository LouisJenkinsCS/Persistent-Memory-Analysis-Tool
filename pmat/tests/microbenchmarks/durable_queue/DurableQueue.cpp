#include "DurableQueue.hpp"

int main(void) {
    auto dq = DurableQueue::alloc<int>(new uint8_t[1024]);
}