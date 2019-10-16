Right now, the interface is still in flux, so some of this is likely to change by the end of this month...

**Registering a Persistent Memory Region**

```c
PMAT_REGISTER(binaryName, addr, size);
```

The 'binaryName' is the name of the file used for the 'shadow' heap , and needs to be unique for
each heap. This file name is how you distinguish which shadow heap is which during verification
and recovery. The 'addr' is a 64-byte aligned (_NEEDS_ to be 64-byte aligned right now, or else
PMAT will reject it when you try registering it!) pointer, and 'size' is the size of persistent
Memory region.

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
the verifier provided and is the 10th verification that is run. In the near future
it will also provide a file for the redirected stderr and stdout, as well as the diagnostic
information such as leaked cache lines and flushes that were leaked due to not being fenced
similar to what is shown on program exit.

**But what about recovery?**

PMAT will produce a complete binary file, that is used in verification; this binary
function can be directly used to recover on. The way to look at it is like this:
A single program history will contain multiple sub-histories that represent what would
happen if a crash occurred at that point in time; see this as your first pass.
In your second pass, you take each of those sub-histories, and if you they have _all_ been
verified to pass your 'smoke test' (which is a software engineering term for a test that is run
to determine whether or not further testing should be done), you can then run PMAT again
on the application with logic used specifically for recovery. If it is desired, you can go even
further by performing more tests on the new sub-history created by the 'recovered' application.
Recovery could be as simple as copying the file binary directly into some persistent region
prior to starting the application. See it as a breadth-first approach to handling verification and
recovery; you begin with an empty state and create sub-histories A, B, and C; if A, B, and C
are all verified  to be correct, run the application on A which produces more sub-histories,
and so on; see below for an example of how a script could be composed.

```bash
# Pass 1
valgrind --tool=pmemcheck --pmat-verifier=verifier ./application
# Verify that all binaries are 'good'...
# Maybe do something to move original files so that new runs don't overwrite...

# Pass 2
for bin in application.bin.*; do
   # Do something so that application runs on state in `bin`...
   valgrind --tool=pmemcheck --pmat-verifier=verifier ./application
done
```

(I do intend to provide a complete example of how recovery can be done via a script
by the end of the month; I really want to use the persistent queue benchmark as a real-world
and _self-contained_ example)

Side Note; Due to Valgrind's thread serialization; it might be okay to run this second pass in
parallel due to only one thread running at any given time, while being constrained by the
memory required to run each instance.

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
