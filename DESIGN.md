# Design of the Persistent Memory Analysis Tool (fork of pmemcheck)

(W.I.P)

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

* When a new persistent memory address is created, we fork
  * The child, of course, gets an up-to-date state of the world.
