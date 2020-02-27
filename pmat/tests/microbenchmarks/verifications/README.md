# README

This directory contains a microbenchmark for determining the maximum number of verifications that can be performed per second.
The verification function is essentially a NOP, and the application will just simulate a crash performed in a tight loop; this
does not measure the overhead of I/O (copying shadow heap, writes to shadow heap, mmaping shadow heap clone in verifier), and
merely shows best case performance.

```
make
valgrind --tool=pmat --verifier=nop_verifier ./nop 5
```
