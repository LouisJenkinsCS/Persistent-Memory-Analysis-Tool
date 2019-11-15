Right now, the interface is still in flux, so some of this is likely to change by the end of this month...

**Registering a Persistent Memory Region**

```c
PMAT_REGISTER(binaryName, addr, size);
PMAT_UNREGISTER_NAME(binaryName);
PMAT_UNREGISTER_ADDR(addr);
```

The 'binaryName' is the name of the file used for the 'shadow' heap , and needs to be unique for
each heap. This file name is how you distinguish which shadow heap is which during verification
and recovery. The 'addr' is a 64-byte aligned (_NEEDS_ to be 64-byte aligned right now, or else
PMAT will reject it when you try registering it!) pointer, and 'size' is the size of persistent
Memory region.

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
valgrind --tool=pmemcheck --pmat-verifier=verifier ./application
```

It should be emphasized that PMAT runs _verification_, and _not_ recovery!
The plan is that the binary files mentioned above are used to run recovery on.
The `binaryName` mentioned above is the prefix to identify which shadow heap
is which, and has the suffix of '[good|bad].\d+', I.E a binaryName of 'dummy.bin'
that passed verification would have 'dummy.bin.good.10' if it successfully passes
the verifier provided and is the 10th verification that is run. Associated with an 'bad'
shadow heap is the `stderr`, `stdout`, and a trace of the leaked cache lines and cache lines
that were flushed but not fenced in the application, which is provided in the hopes that it will
aid in fixing bugs in the application.


**PMAT Library Includes**

```c
#include <valgrind/pmemcheck.h>
```

After doing a `sudo make install` you should be able to access the above via your
typical system includes. Right now it still has the `pmemcheck` name, but that is of
course going to change by the end of this month. The macros mentioned above
need to be placed in your program, _but_ the program can and will run without
Valgrind running, so no additional compiler directives are needed, without going
into the details.
