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
valgrind --tool=pmat --verifier=linked_list_verifier ./linked_list g 1024
```

## Bad Test

**Should fail**

```
valgrind --tool=pmat --verifier=linked_list_verifier ./linked_list b 1024
```

## Pmemcheck + Pmreorder

**Create Pool**

```bash
pmempool create -v blk $((1024)) -s $((1024 * 1024 * 128)) linked_list_pmemcheck.bin
```

**Make**

```bash
make CFLAGS="-DDO_PMEMCHECK -I/home/louisjenkinscs/GitHub/Pmemcheck/include/" LDFLAGS="-lpmem"
```

**Pmemcheck**

```bash
for i in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576; do echo $i; time ~/GitHub/Pmemcheck/bin/valgrind --tool=pmemcheck --log-stores=yes --log-stores-stacktraces=yes --log-stores-stacktraces-depth=2 -q ./linked_list b $i linked_list_pmemcheck.bin &> traces-$i.txt; done
```

**Pmreorder**

```bash
for i in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576; do echo $i; time python3 ~/GitHub/pmdk/src/tools/pmreorder/pmreorder.py -l traces-$i.txt -r ReorderPartial -o pmreorder_out.log -x PMREORDER_MARKER_NAME=ReorderPartial -c prog -p ./linked_list_verifier c linked_list_pmemcheck.bin &> /dev/null; done
```

