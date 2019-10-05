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
