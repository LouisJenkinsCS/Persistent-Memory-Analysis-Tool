## TODO List

### Tool

1. Move `*.bin.good` and `*.bin.bad` files into a specific directory.
2. Generate a file containing the state, consisting of unique cache lines that have not been written back yet, and flushes without a fence.
3. Experiment with automatically creating a shared-memory file by using `open` with `/dev/shm` (equivalent to `shm_open`)

### Tests

1. Create [persistent non-blocking queue](https://dl.acm.org/citation.cfm?id=3178490) and test on that... 
2. Create tests that use fixed address mappings between both application and verifier; need to be confident that this works.
3. ~~Add Dependency directory for things like libqsbr~~
