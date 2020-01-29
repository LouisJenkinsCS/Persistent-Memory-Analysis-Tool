# Tests for the Persistent Memory Analysis Tool (PMAT)

This directory contains simple unit tests that check whether or not
the testing tool is capable of finding bugs in the specific patterns.

**To Build**

```
make tests CFLAGS="-I/path/to/valgrind/headers"
```

*Note:* `make tests` is only required at the root of tests as Valgrind attempts to call `make`. In the future, I will overhaul the entire
build system for PMAT to conform with the rest of valgrind, as discussed in https://github.com/LouisJenkinsCS/Persistent-Memory-Analysis-Tool/issues/10.

**To Run (Example)**

```
valgrind --tool=pmat --pmat-verifier=in-order-store_verifier ./out-of-order-store
```

The output `*.bin*` files contain the state of the sample used during verification.
`*.bin.good*` contains a snapshot of the shadow heap that passed the verifier, and the
`*.bin.bad*` contains a snapshot of the shadow heap that failed the verifier.

Verifiers are simple programs that take as arguments: `progName N file1 file2 ... fileN`
and return `PMAT_VERIFICATION_FAILURE` when the shadow heap fails verification, and
returns 0 if it passes verification. Verification should be seen as a simple 'smoke test';
it can be used to determine 'at-a-glance' that the state of the shadow heap is consistent,
but closer inspection should be made, I.E via a hex editor, or even by recovering directly
from the heap (I.E by using `mmap` or copying the heap into persistent memory before beginning
your application).

## Test Combination

```
valgrind --tool=pmat --pmat-verifier=in-order-store_verifier ./in-order-store
valgrind --tool=pmat --pmat-verifier=in-order-store_verifier ./out-of-order-store
valgrind --tool=pmat --pmat-verifier=openmp_test_verifier ./openmp_test
```
