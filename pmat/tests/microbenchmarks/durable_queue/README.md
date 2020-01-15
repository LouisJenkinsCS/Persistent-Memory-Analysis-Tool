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
valgrind --tool=pmat --pmat-verifier=durable_queue_verifier ./durable_queue 5
```
