#!/bin/bash

for i in `seq 0 20`; do
    for j in `seq 0 20`; do
        for sched in 'no' 'yes' 'random'; do
            echo "Running $((1<<$i))-$((1<<$j))-$sched"
            valgrind --fair-sched=$sched --tool=pmat --aggregate-dump-only=yes --verifier=durable_queue_verifier --rng-seed=$((0x1BAD5EED)) --num-wb-entries=$((1 << $i)) --num-cache-entries=$((1 << $j)) ./durable_queue 300 &> $((1<<$i))-$((1<<$j))-$sched.out
            cat $((1<<$i))-$((1<<$j))-$sched.out
        done
    done
done