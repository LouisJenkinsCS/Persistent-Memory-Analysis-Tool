/*
    This file contains a basic pseudo garbage collector,
    which utilizes both a concurrent memory reclamation system,
    being Epoch-Based Reclamation (EBR). Objects that are deferred
    for reclamation are held in 'limbo' lists associated with a given
    epoch. Once no thread is in a given epoch, we reclaim them by adding
    them back to the free-list that can be allocated later. This is
    hand-tailored for this persistent queue example, as the heap is fixed
    size (in fact, the heap is a fixed-size persistent memory region),
    and will never grow; it is intended that when there is no space left,
    that the operation instead attempts to dequeue to balance out the
    memory consumption.

    Code adapted from https://github.com/chapel-lang/chapel-attic/tree/collaborators/QSBR
    Original code was originally designed and developed by myself.
*/

#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Initializes our pseudo garbage collector with the provided (hopefully) persistent memory region and size
// Creates a fixed number of blocks up front to be managed/recycled and tracked
void gc_init(void *heap, size_t sz, size_t blockSize);
void gc_defer_deletion(void *data);
void gc_exit(void);

