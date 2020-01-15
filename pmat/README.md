Right now, the interface is still in flux, so some of this is likely to change by the end of this month...

**Registering a Persistent Memory Region**

```c
PMAT_REGISTER(binaryName, addr, size);
PMAT_UNREGISTER_BY_NAME(binaryName);
PMAT_UNREGISTER_BY_ADDR(addr);
```

The 'binaryName' is the name of the file used for the 'shadow' heap , and needs to be unique for
each heap. This file name is how you distinguish which shadow heap is which during verification
and recovery. The 'addr' is a 64-byte aligned (_NEEDS_ to be 64-byte aligned right now, or else
PMAT will reject it when you try registering it!) pointer, and 'size' is the size of persistent
Memory region. Make sure to unregister before freeing the memory!

To mark a particular portion of a persistent memory region as transient, which is useful when you,
say have a field in a `struct` that you do not care about the persistence of and do not want this to
show up when trying to debug leaked and unfenced cache lines, you can use the following macro. Marking
something transient will result in the value never being updated in the shadow heap.

```c
PMAT_TRANSIENT(addr, sz);
```

**Automatic Crash Simulation**

```c
PMAT_CRASH_DISABLE();
PMAT_CRASH_ENABLE();
PMAT_FORCE_CRASH();
```

Whether or not PMAT's automatic crash simulation is enabled by default is liable
to change (with options to toggle default via command line argument, of course),
but in summary, the automatic crash simulation has a set chance (currently 5%)
to occur on any store to persistent memory, after a flush, and before-and-after
a fence. This can be very excessive, so I will definitely be tuning this to be more
dynamic and reasonable; for now it may be best to `PMAT_CRASH_DISABLE` and
force a crash at strategic points via `PMAT_FORCE_CRASH`, although for full
testing it would be ideal to have both automatic crash and strategic calls.

**Registering a _Verification_ Function**

```bash
valgrind --tool=pmat --pmat-verifier=verifier ./application
```

The plan is that the binary files mentioned above are used to run recovery on.
The `binaryName` mentioned above is the prefix to identify which shadow heap
is which, and has the suffix of '[good|bad].\d+', I.E a binaryName of 'dummy.bin'
that passed verification would have 'dummy.bin.good.10' if it successfully passes
the verifier provided and is the 10th verification that is run. Associated with a 'bad'
shadow heap is the `stderr`, `stdout`, and a trace of the leaked cache lines and cache lines (`dump`)
that were flushed but not fenced in the application, which is provided in the hopes that it will
aid in fixing bugs in the application; this has a fixed prefix `bad-verification-\d+`.

As well, statistical information such as the mean, minimum, maximum, and variance of times for running the verifier
is provided. This can provide some way to measure how expensive recovery/verification is, and may help when it comes to
tuning how fast and efficient is is, which is especially important when testing. For example...

```
==15631== 47 out of 656 verifications failed...
==15631== Verification Function Stats (seconds):
==15631==       Minimum:1.583812e-3
==15631==       Maximum:8.641710e-2
==15631==       Mean:3.345374e-3
==15631==       Variance:2.132564e-5
```

"Why is verification/recovery so slow? Why is testing taking so long?" - The above answers that question by showing that as
time goes on, the average time of verification increases as the complexity of the underlying heap increases; that is, the more data there is to check, the longer it takes. Can this be optimized more? Maybe; this up to the user to decide whether or not it is worth it or not.


**PMAT Library Includes**

```c
#include <valgrind/pmat.h>
```

After doing a `sudo make install` you should be able to access the above via your
typical system includes. 
