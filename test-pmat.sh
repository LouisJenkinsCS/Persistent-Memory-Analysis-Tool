cd pmat/tests \
      && make tests \
      && time valgrind --tool=pmat --verifier=in-order-store_verifier ./in-order-store \
      && time valgrind --tool=pmat --verifier=in-order-store_verifier ./out-of-order-store \
      && time valgrind --fair-sched=yes --tool=pmat --verifier=openmp_test_verifier ./openmp_test \
      && time valgrind --tool=pmat --verifier=split-store_verifier ./split-store \
      && make clean \
      && cd microbenchmarks/durable_queue \
      && make \
      && time valgrind --fair-sched=yes --tool=pmat --verifier=durable_queue_verifier ./durable_queue 60 \
      && time valgrind --tool=pmat --verifier=durable_queue_verifier ./durable_queue_ordered 60 \
      && make clean \
      && cd ../linked_list \
      && make \
      && time valgrind --tool=pmat --verifier=linked_list_verifier ./linked_list g $((8 * 1024)) \
      && time valgrind --tool=pmat --verifier=linked_list_verifier ./linked_list b $((8 * 1024)) \
      && make clean \
      && cd ../verifications \
      && make \
      && time valgrind --tool=pmat --verifier=nop_verifier ./nop 10 \
      && make clean
