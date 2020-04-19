#!/bin/bash

P1=$((1<<0))
P2=$((1<<1))
P3=$((1<<2))
P4=$((1<<3))
P5=$((1<<4))

for i in $P1 $P2 $P3 $P4 $P5; do
    make clean
    make CFLAGS="-DDURABLE_QUEUE_BUG=$i -DSKIP_SANITY_CHECK"
    for sched in 'yes' 'no' 'random'; do
        for sample in `seq 1 100`; do
            valgrind --tool=pmat --fair-sched=$sched \
            --terminate-on-error=yes  --verifier=durable_queue_verifier \
            ./durable_queue 60 &> "bug-$sched.$i.$sample.out"
        done;
    done;
    make clean
    make CFLAGS="-DDURABLE_QUEUE_BUG_FLUSHOPT=$i -DSKIP_SANITY_CHECK"
    for sched in 'yes' 'no' 'random'; do
        for sample in `seq 1 100`; do
            valgrind --tool=pmat --fair-sched=$sched \
            --terminate-on-error=yes  --verifier=durable_queue_verifier \
            ./durable_queue 60 &> "bug_flushopt-$sched.$i.$sample.out"
        done;
    done;
done;