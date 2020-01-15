## Build

```
make
```

## Cleanup

```
make clean
```

## Good Test

**Should not ever fail!**

```
valgrind --tool=pmat --pmat-verifier=linked_list_verifier ./linked_list g 1024
```

## Bad Test

**Should fail**

```
valgrind --tool=pmat --pmat-verifier=linked_list_verifier ./linked_list b 1024
```
