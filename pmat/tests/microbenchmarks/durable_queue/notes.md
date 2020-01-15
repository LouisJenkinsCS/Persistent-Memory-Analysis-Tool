```
valgrind --tool=pmat --pmat-verifier=./durable_queue_verifier ./durable_queue 60
==17518== PMAT-0.1, Persistent Memory Analysis Tool
==17518== University of Rochester
==17518== Using Valgrind-3.14.0 and LibVEX; rerun with -h for copyright info
==17518== Command: ./durable_queue 60
==17518==
Sanity checking queue...
Finished enqueue...
Finished dequeue...
Sanity check complete, beginning benchmark for 60 seconds...
Number of threads: 4
Thread 3 performed 10852 operations
Thread 2 performed 10851 operations
Thread 1 performed 10851 operations
Thread 0 performed 10851 operations
Performed 43405 operations
==17518==
==17518== Number of cache-lines not made persistent: 0
==17518== Number of cache-lines flushed but not fenced: 0
==17518== 0 out of 3864 verifications failed...
==17518== Verification Function Stats (seconds):
==17518==       Minimum:5.757788e-3
==17518==       Maximum:1.868702e-2
==17518==       Mean:6.488075e-3
==17518==       Std:8.981684e-7
==17518==       Variance:8.981684e-7
```
 
Notes: Interesting to note, but I actually found a potential bug in my verification mechanism and in some of the additional metadata used to keep track of the number of expected nodes. So, I add 64 bytes (cache line size) before all of the fields in the queue, the first 16 bytes used to keep track of the number of enqueue operations and dequeue operations; PMAT was actually able to show me a crucial flaw in that its possible for the number of dequeue operations to be at least one less than the actual number of dequeue operations that have persisted; I.E a node can be dequeued, but a crash occurs before I increment the counter representing the number of the dequeue operations. Very cool!
 
Also I had to make each thread randomly performed enqueue/dequeue in lock-step (I.E a barrier at beginning of each iteration of the loop) due to needing to have a stop-the-world approach for memory reclamation. This may also factor into the low number of verifications being performed. In the above, verification runs for 6.488075e-3 * 3864 = 25.0699218 seconds, or 42% of the time. The other overhead is very likely from instrumentation, having so many barriers per iteration, stop-the-world garbage collection, and the fact that this is running in a VM on my old shoddy Macbook air laptop.
 