## Build

```
make
```

## Cleanup

```
make clean
```

## Test

```
valgrind --tool=pmemcheck --pmat-verifier=durable_queue_verifier ./durable_queue 5
```
