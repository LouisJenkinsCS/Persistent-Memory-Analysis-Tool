#pragma once

#include <cstdint>
#include <cstdbool>
#include <vector>
#include <iostream>

#define DQ_MAX_THREADS 16
#define DQ_EMPTY -1

namespace DurableQueue
{
    namespace Memory
    {
        namespace DWord 
        {
            typedef struct uint128 {
                uint64_t lo;
                uint64_t hi;

                uint64_t get_ptr(void) { return lo; }
                uint64_t get_seq(void) { return hi; }
                void set_ptr(uint64_t uptr) { lo = uptr; }
                void set_seq(uint64_t uptr) { hi = uptr; }
            } uint128_t;

            // src: Address of a 16-byte aligned address or else General Protection Fault (GPF)
            // cmp: Expected value
            // with: New value to replace old
            // Returns: If successful or not
            static inline int cas128bit(uint128_t *src, uint128_t *cmp, uint128_t *with) {
                char result;
                __asm__ __volatile__ ("lock; cmpxchg16b (%6);"
                    "setz %7; "
                    : "=a" (cmp->lo),
                    "=d" (cmp->hi)
                    : "0" (cmp->lo),
                    "1" (cmp->hi),
                    "b" (with->lo),
                    "c" (with->hi),
                    "r" (src),
                    "m" (result)
                    : "cc", "memory"); 
                return result;
            }

            static inline void write128bit(void *srcvp, void *valvp) {
                uint128_t __attribute__ ((aligned (16))) with_val = * static_cast<uint128_t *>(valvp);
                uint128_t __attribute__ ((aligned (16))) cmp_val = * static_cast<uint128_t *>(srcvp);
                uint128_t *src = static_cast<uint128_t *>(srcvp);
                uint128_t *cmp = &cmp_val;
                uint128_t *with = &with_val;
                while (!cas128bit(src, cmp, with)) ;
            }

            // Special-case which will update the ABA count of valvp to be
            // one plus the srcvp. This is needed as ABA count needs to be monotonically
            // increasing.
            static inline void write128bit_special(void *srcvp, void *valvp) {
                uint128_t __attribute__ ((aligned (16))) with_val = * static_cast<uint128_t *>(valvp);
                uint128_t __attribute__ ((aligned (16))) cmp_val = * static_cast<uint128_t *>(srcvp);
                uint128_t *src = static_cast<uint128_t *>(srcvp);
                uint128_t *cmp = &cmp_val;
                uint128_t *with = &with_val;

                with->hi = cmp->hi + 1;
                while (!cas128bit(src, cmp, with)) {
                    with->hi = cmp->hi + 1;
                }
            }

            // srcvp: Address of a 16-byte aligned address or else General Protection Fault (GPF)
            // valvp: New value to replace the old
            // retvalp: Stores the old value
            static inline void exchange128bit(void *srcvp, void *valvp, void *retvalvp) {
                uint128_t __attribute__ ((aligned (16))) with_val = * static_cast<uint128_t *>(valvp);
                uint128_t __attribute__ ((aligned (16))) cmp_val = * static_cast<uint128_t *>(srcvp);
                uint128_t *src = static_cast<uint128_t *>(srcvp);
                uint128_t *cmp = &cmp_val;
                uint128_t *with = &with_val;
                while (!cas128bit(src, cmp, with)) ;
                *(uint128_t *) retvalvp = cmp_val; 
            }

            // Special-case which will update the ABA count of valvp to be
            // one plus the srcvp. This is needed as ABA count needs to be monotonically
            // increasing.
            static inline void exchange128bit_special(void *srcvp, void *valvp, void *retvalvp) {
                uint128_t __attribute__ ((aligned (16))) with_val = * static_cast<uint128_t *>(valvp);
                uint128_t __attribute__ ((aligned (16))) cmp_val = * static_cast<uint128_t *>(srcvp);
                uint128_t *src = static_cast<uint128_t *>(srcvp);
                uint128_t *cmp = &cmp_val;
                uint128_t *with = &with_val;
                
                with->hi = cmp->hi + 1;
                while (!cas128bit(src, cmp, with)) {
                    with->hi = cmp->hi + 1;
                }
                *(uint128_t *) retvalvp = cmp_val; 
            }
            
            static inline void read128bit(void *srcvp, void *dstvp) {
                uint128_t __attribute__ ((aligned (16))) src_val = * static_cast<uint128_t *>(srcvp);
                uint128_t __attribute__ ((aligned (16))) cmp_val = src_val;
                uint128_t __attribute__ ((aligned (16))) with_val = src_val;
                uint128_t *src = static_cast<uint128_t *>(srcvp);
                uint128_t *cmp = &cmp_val;
                uint128_t *with = &with_val;
                cas128bit(src, cmp, with);
                * static_cast<uint128_t *>(dstvp) = cmp_val;
            }

            template <class T>
            class ABA {
                public:
                    ABA() : ptr() {}
                    ABA(uint128_t u128) : ptr(u128) {}
                    ABA(T* t) {
                        ptr.set_seq(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<void *>(t))));
                    }
                    T& operator*() { return *reinterpret_cast<T*>(ptr.get_ptr()); }
                    T* operator->() { return reinterpret_cast<T*>(ptr.get_ptr()); }
                    operator T*() { return reinterpret_cast<T*>(ptr.get_ptr()); }
                    uint64_t get_seq() { return ptr.get_seq(); }
                    uint128_t& to_ptr() { return ptr; }
                private:
                    uint128_t __attribute__ ((aligned (16))) ptr;
            };

            // Anti-ABA pointer; wraps pointer in 128-bit word.
            template <class T>
            class ABAPtr {
                public:
                    bool CAS(ABA<T>& expected, ABA<T>& with) {
                        uint128_t& abaRef = aba.to_ptr();
                        uint128_t& expectedRef = expected.to_ptr();
                        uint128_t& withRef = with.to_ptr();
                        return DWord::cas128bit(&abaRef, &expectedRef, &withRef);
                    }

                    void store(ABA<T>& value) {
                        uint128_t& abaRef = aba.to_ptr();
                        uint128_t& valueRef = value.to_ptr();
                        DWord::write128bit(&abaRef, &valueRef);
                    }

                    ABA<T> load() {
                        uint128_t& abaRef = aba.to_ptr();
                        ABA<T> aba;
                        DWord::read128bit(&abaRef, &aba);
                        return aba;
                    }

                    void operator=(ABA<T>& other) {
                        store(other);
                    }

                    void operator=(T *other) {
                        uint128_t& abaRef = aba.to_ptr();
                        uint128_t valueRef;
                        valueRef.set_ptr(reinterpret_cast<uint64_t>(other));
                        DWord::write128bit_special(&abaRef, &valueRef);
                    }
                private:
                    ABA<T> aba;
            };
        }

        template <class T>
        class FreeListNode {
            public:
                FreeListNode(T t) : obj(t), next(nullptr) {}
                T& get_obj() { return obj; }
                FreeListNode<T> *get_next() { return next; }
                void set_next(FreeListNode<T> *node) { next = node; }
            private:
                T obj;
                FreeListNode<T> *next;
        };

        // FreeList is a trieber stack which used tagged/counted pointers.
        // Note: No new nodes will be created (for now...)
        // TODO: Build custom allocator for fun!
        template <class T>
        class FreeList {
            public:
                void push(T t) {
                    Memory::DWord::ABA<FreeListNode<T>> node(new FreeListNode<T>(t));
                    Memory::DWord::ABA<FreeListNode<T>> _head;
                    do {
                        _head = head.load();
                        node->set_next(_head);
                    } while(!head.CAS(_head, node));
                }

            private:
               Memory::DWord::ABAPtr<FreeListNode<T>> head;
            
        };
    }
    
    template <class T>
    class DurableQueueNode {
        public:
            DurableQueueNode() : obj(), next(nullptr) {}
            DurableQueueNode(T& t) : obj(t), next(nullptr) {}

            T get_obj() { return obj; }
            DurableQueueNode<T> *get_next() { return next; }
            void set_obj(T& obj) { return obj; }
            void set_next(DurableQueueNode<T> *node) { next = node; }
        private:
            T obj;
            DurableQueueNode<T> *next;    
    };

    template <class T, T NilT = T()>
    class DurableQueue {
        public:
            // Heap is leftover persistent memory region that we
            // are utilizing to create nodes from.
            DurableQueue(uint8_t *heap, size_t sz) {
                this->heap = heap;
                this->freeList = new Memory::FreeList<DurableQueueNode<T> *>();
                this->allocList = new std::vector<DurableQueueNode<T> *>();

                // Initialize allocList
                int allocSz = 0;
                while (allocSz + sizeof(DurableQueueNode<T>) < sz) {
                    this->allocList->push_back(new (heap + allocSz) DurableQueueNode<T>());
                    allocSz += sizeof(DurableQueueNode<T>);
                }
                
                // Initialize freeList
                for (auto node : *allocList) {
                    freeList->push(node);
                }
                
                // Initialize return value array
                for (T& retval : retvals) {
                    retval = NilT;
                }

                // Initialize head and tail with dummy node
                T tmp = NilT;
                DurableQueueNode<T> *dummy = new DurableQueueNode<T>(tmp);
                this->head = dummy;
                this->tail = dummy;
            }

        private: 
            // Persistent head of queue
            Memory::DWord::ABAPtr<DurableQueueNode<T>> head;
            // Persistent tail of queue
            Memory::DWord::ABAPtr<DurableQueueNode<T>> tail;
            // Persistent array of return values for threads
            T retvals[DQ_MAX_THREADS];
            // Transient remaining portion of heap
            uint8_t *heap;
            // Transient FreeList
            Memory::FreeList<DurableQueueNode<T>*> *freeList;
            // Transient AllocList
            std::vector<DurableQueueNode<T>*>* allocList;                        
    };

    template <class T>
    static DurableQueue<T> *alloc(uint8_t *heap, size_t sz) {
        return new (heap) DurableQueue<T>(heap + sizeof(DurableQueue<T>), sz - sizeof(DurableQueue<T>));
    }
}