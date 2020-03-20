#!/bin/bash

for i in `seq 0 20`; do
    for j in `seq 0 20`; do
        for sched in 'no' 'yes' 'random'; do
            for quantum in 1000 100000; do
                for randomize in 'no' 'yes'; do
                    cacheSize=$((1<<$i))
                    wbSize=$((1<<$j))
                    fname="cacheSize=$cacheSize-wbSize=$wbSize-sched=$sched-quantum=$quantum-randomize=$randomize"
                    echo "Running $fname"
                    `which time` -f %e valgrind \
                        --fair-sched=$sched \
                        --tool=pmat \
                        --aggregate-dump-only=yes \
                        --verifier=durable_queue_verifier \
                        --rng-seed=$((0x1BAD5EED)) \
                        --num-wb-entries=$wbSize \
                        --num-cache-entries=$cacheSize \
                        --randomize-quantum=$randomize \
                        --scheduling-quantum=$quantum \
                        ./durable_queue 300 &> $fname.out
                    cat $fname.out
                done
            done
        done
    done
done