```
> valgrind --tool=pmat --pmat-verifier=linked_list_verifier ./linked_list g $((1024*1024))

==18582== PMAT-0.1, Persistent Memory Analysis Tool
==18582== University of Rochester
==18582== Using Valgrind-3.14.0 and LibVEX; rerun with -h for copyright info
==18582== Command: ./linked_list g 1048576
==18582== 
    
==18582== 
==18582== Number of cache-lines not made persistent: 0
==18582== Number of cache-lines flushed but not fenced: 0
==18582== 0 out of 62671 verifications failed...
==18582== Verification Function Stats (seconds):
==18582==       Minimum:6.955530e-4
==18582==       Maximum:3.093670e-1
==18582==       Mean:1.331822e-2
==18582==       Std:7.153950e-5
==18582==       Variance:7.153950e-5
```

## Analysis

As the benchmark goes on, the list increases linearly in size; hence each verification function call takes increasingly more time. This run
took about 15 minutes, due to having the list go from length 0 to 1M. This is still vastly superior to `pmereorder` taking 2.5 to 3 entire
days on their ReorderPartial, the most lightweight engine available. Plus the fact this is entirely online is of huge benefit.