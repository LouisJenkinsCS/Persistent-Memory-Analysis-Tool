## Build

```
make CFLAGS="-I/path/to/valgrind/headers"
```

## Cleanup

```
make clean
```

## Test

```
valgrind --tool=pmat --verifier=durable_queue_verifier ./durable_queue 5
```
