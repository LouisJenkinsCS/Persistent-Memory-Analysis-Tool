# Design of the Persistent Memory Analysis Tool (fork of pmemcheck)

## Replicating Client State via a Shadow Heap

* Pointers declared to the Valgrind host as being 'persistent' get their own shadow heap
  * Shadow heap represents a _file_ that is written to in a way that simulates the out-of-order nature of the CPU
  * Due to constructs such as `mmap` and `shm*` being unavailable in Valgrind, we use `lseek` and `write`
    * Valgrind builds without linking in standard libraries, likely for portability reasons
    * Makes many assumptions that would 'break' Valgrind if circumvented without non-trivial effort to remedy
* Replicated Client State that is written to file is used by a child process
  * Valgrind does offer ability to `fork` and then `execv` a verification process
    * Verification will be provided the file name, and have freedom to mmap into memory as they please to verify.
  * When it is time to verify, a 'fork' of the file must be created via copying into a newer file
    * Older file is give to verification process which can then begin immediately.
    

## Store/Flush/Fence Tracing

* Stores to persistent memory are checked at runtime via valgrind instrumentation
  * Instrumentation occurs the first time a portion of code is to be executed, I.E Just-In-Time
  * Instrumentation allows for certain 'hooks' to be called whenever some event occurs
  * Unlike _static_ analysis tools, valgrind can intercepts calls to _all_ code, including dynamic libraries like glibc
* Stores are managed at the granularity of cache lines...
  * Mapping of cache line of store to a data structure representing cache line...
  * Cache line keeps a list of store metadata that includes line number, function name, etc., information
    * Enable tracing of origins of store that did not persist after a program crash.
  * Each cache line also keeps a bitmap of 'dirty bits' that determine what should and shoud not be written back...
* Flushes to a cache line will place cache line to a write-buffer
  * Write-buffer is randomly flushed when full, to simulate out-of-order write-back of cache lines
  * Each write buffer entry keeps track of the thread id and origin of flush
* Fences will flush all cache lines for a particular thread
  * Ensures that all write-buffer entries are written back for that thread

## Verification

* A process is created based on user arguments and is passed a file name
  * This file can then be `mmap`'d and verified
  * Not as fast as actual persistent memory, but necessary compromise
  * Multiple verification processes can be performed concurrently.
* Given a system with N threads, at most N - 2 verification processes will occur concurrently
  * Thread serialization in Valgrind makes it 'not so bad' to do so since only one thread runs at any given time
  * Child will keep an array of `(pid, fileName)` associatied with current running grandchildren and their files
  * When a grandchild finishes, can collect return value for verification; if bad, keep bad `fileName` so can be analyzed
  * Child will poll on pipe for data, and while not busy, will check results of verification
  * Further requests for verification are queued up for later
* Saving all files provides option to attempt 'recovery' from each individual file, to further test verification.

## Debug Information

* When a verification process fails, we do the following...
  * Do not delete the faulty binary file that is the 'shadow heap'...
  * Associate with the prefix of the faulty binary file, the state of...
    * The cache, including the location of _all_ stores that have not yet been written back
    * The write-buffer, including the location of _all_ flushes and affiliated stores...
    * Perhaps the state of all the other threads when the power-failure occurred?
  * Should be possible to determine "where", "when", and let the programmer infer "why"
    * A `store` at `program:L126` with did not persist (show value written)
    * A `flush` at `program:L127` was did not reach a `fence` and was not written-back (show flush and store info)
    * Show time may be helpful, as it can identify how long this leak has occurred (microseconds, that's okay... minutes? Thats bad!)

## Intermediate Steps

* Maintain only the parent; parent writes directly to a file; forks to spawn child process to handle verification on file
  * Experiment with `fork` to create a child with current write-buffer and cache
    * Child will `fork` to create grandchild verification process and will monitor it
    * Child still has access to write-buffer and cache and so can handle reporting errors
  * Parent has to handle creating copy of 'file' and updating it, but is more natural and intuitive
