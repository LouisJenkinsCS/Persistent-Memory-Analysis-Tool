## TODO List

### Tool

1. Move `*.bin.good` and `*.bin.bad` files into a specific directory.
2. Generate a file containing the state, consisting of unique cache lines that have not been written back yet, and flushes without a fence.

### Tests

1. Create [persistent non-blocking queue](https://dl.acm.org/citation.cfm?id=3178490) and test on that... 
2. Create tests that use fixed address mappings between both application and verifier; need to be confident that this works.
