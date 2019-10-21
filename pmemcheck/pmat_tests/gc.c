#include "gc.h"

#define TLS_CHECK 1

// Thread-specific meta data.
struct tls_node {
  /*
    The current observed epoch. This is used to ensure that writers
    do not delete an older instance that corresponds to this epoch.
  */
  atomic_uint_least64_t epoch;
  /*
    Whether or not this thread is parked in the runtime. If the thread is
    parked, it is ignored during memory reclamation checks.
  */
  atomic_uint_least64_t parked;
  /*
    List of objects that are queued for deferred deletion, sorted by target_epoch. 
    We defer deletion if we find that while scanning the list of all thread-specific
    data, they are not up-to-date on that epoch. By sorting by target_epoch, we can
    make shortcuts by removing a large bulk of data at once. The list is protected
    by a lightweight spinlock, but is acquired using pseudo-TestAndSet operations
    due to low contention.
  */
  atomic_uint_least64_t spinlock;
  struct defer_node *deferList;

  /*
    Recycled defer nodes that can be reused. This is not a thread-safe field and
    only be accessed by the thread that owns this node.
  */
  struct defer_node *recycleList;

  /*
    Next TLS node. Note that this field can be updated atomically by newer threads
    and so should be accessed via atomics.
  */
  struct tls_node *next;
};

struct free_node {
  void *data;
  atomic_uintptr_t next;
};

struct defer_node {
  uint64_t targetEpoch;
  void *data;
  atomic_uintptr_t next;
};

// List of thread-local data.
struct tls_node *tls_list;
__thread struct tls_node *tls;

// Free List - Used to grab work from
struct free_node *free_list;
atomic_bool free_list_lock = ATOMIC_VAR_INIT(0);

// The heap (should be our persistent memory region)
void *persistent_heap_base;
size_t persistent_heap_size;

// Global epoch...
atomic_uint_least64_t global_epoch = ATOMIC_VAR_INIT(0);

static inline void error(const char *str) {
    fprintf(stderr, str);
    exit(EXIT_FAILURE);
}

static inline struct tls_node *get_tls_list(void);
static inline struct tls_node *get_tls_list(void) {
  #if TLS_CHECK
  if (!tls_list) {
    error("'chpl_qsbr_tls_list' is NULL!\n");
  }
  #endif
  return (struct tls_node *) atomic_load(&tls_list);
}

static inline uint64_t get_epoch(struct tls_node *node);
static inline uint64_t get_epoch(struct tls_node *node) {
  return atomic_load(&node->epoch);
}

static inline void set_epoch(struct tls_node *node, uint64_t epoch);
static inline void set_epoch(struct tls_node *node, uint64_t epoch) {
  return atomic_store(&node->epoch, epoch);
}

static inline uint64_t get_global_epoch(void);
static inline uint64_t get_global_epoch(void) {
  return atomic_load(&global_epoch);
}

static inline uint64_t advance_global_epoch(void);
static inline uint64_t advance_global_epoch(void) {
  return atomic_fetch_add(&global_epoch, 1) + 1;
}

static inline void observe_epoch(struct tls_node *node);
static inline void observe_epoch(struct tls_node *node) {
  atomic_store(&node->epoch, get_global_epoch());
}

static inline void acquire_spinlock(struct tls_node *node, uintptr_t value);
static inline void acquire_spinlock(struct tls_node *node, uintptr_t value) {
  // Fast path: Quick acquire spinlock
  uint_least64_t expected = 0;
  if (!atomic_compare_exchange_strong(&node->spinlock, &expected, value)) {
    // Slow path: TestAndTestAndSet. Note that we cannot yield or
    // we will end up calling the checkpoint.
    while (1) {
      uint64_t spinlock = atomic_load(&node->spinlock);
      
      #if TLS_CHECK
      // Occurs if current task gets preempted while we hold spinlock
      // or if we attempted to recurse on a spinlock/forgot to release 
      if (spinlock == value) {
        error("Attempt to acquire spinlock when already acquired...\n");
      }
      #endif

      expected = 0;
      if (spinlock == 0 
        && atomic_compare_exchange_weak(&node->spinlock, &expected, value)) {
        break;
      }
    }
  }
}

static inline void release_spinlock(struct tls_node *node);
static inline void release_spinlock(struct tls_node *node) {
  #if TLS_CHECK
  if (atomic_load(&node->spinlock) != (uintptr_t) node) {
    error("Attempt to release spinlock when not held by us...");
  }
  #endif

  atomic_store(&node->spinlock, 0);
}

// Requires spinlock.
static inline struct defer_node *get_defer_list(struct tls_node *node);
static inline struct defer_node *get_defer_list(struct tls_node *node) {
  return node->deferList;
}

// Requires spinlock. Pops all items less than or equal to 'epoch'.
static inline struct defer_node *pop_bulk_defer_list(struct tls_node *node, uint64_t epoch);
static inline struct defer_node *pop_bulk_defer_list(struct tls_node *node, uint64_t epoch) {
  #if TLS_CHECK
  if (atomic_load(&node->spinlock) != (uintptr_t) node) {
    error("Attempt to mutate data without holding spinlock...");
  }
  #endif

  struct defer_node *dnode = node->deferList, *prev = NULL;
  while (dnode != NULL) {
    if (epoch >= dnode->targetEpoch) {
      break;
    }

    prev = dnode;
    dnode = dnode->next;
  }

  // If both nodes are NULL, then the list was actually empty
  if (dnode == NULL && prev == NULL) {
    return NULL;
  }

  // If prev is NULL then we have the actual head of the list
  if (prev == NULL) {
    node->deferList = NULL;
    return dnode;
  }

  // If neither are NULL, we need to correct list so previous points to NULL
  prev->next = NULL;
  return dnode;
}

// Requires spinlock.
static inline struct defer_node *pop_all_defer_list(struct tls_node *node);
static inline struct defer_node *pop_all_defer_list(struct tls_node *node) {
  #if TLS_CHECK
  if (atomic_load(&node->spinlock) != (uintptr_t) node) {
    error("Attempt to mutate data without holding spinlock...");
  }
  #endif

  struct defer_node *head = node->deferList;
  node->deferList = NULL;
  return head;
}

// Requires spinlock.
static inline void push_defer_list(struct tls_node *node, struct defer_node *dnode);
static inline void push_defer_list(struct tls_node *node, struct defer_node *dnode) {
  #if TLS_CHECK
  if (atomic_load(&node->spinlock) != (uintptr_t) node) {
    error("Attempt to mutate data without holding spinlock...");
  }
  #endif

  dnode->next = node->deferList;
  node->deferList = dnode;
}

// Must be called from the thread owning the node.
static inline struct defer_node *pop_recycle_list(struct tls_node *node);
static inline struct defer_node *pop_recycle_list(struct tls_node *node) {
  struct defer_node *dnode = node->recycleList;
  
  // If empty, return fresh allocation
  if (dnode == NULL) {
    return calloc(1, sizeof(struct defer_node));
  }

  node->recycleList = dnode->next;

  // Sanitize before return...
  memset(dnode, 0, sizeof(struct defer_node));
  return dnode;
}

// Must be called from the thread owning the node.
static inline void push_recycle_list(struct tls_node *node, struct defer_node *dnode);
static inline void push_recycle_list(struct tls_node *node, struct defer_node *dnode) {
  dnode->next = node->recycleList;
  node->recycleList = dnode;
}

// Deletes data held by 'dnode', does not free 'dnode'
static inline void delete_data(struct defer_node *dnode);
static inline void delete_data(struct defer_node *dnode) {
  chpl_mem_free(dnode->data, 0, 0);
}

// Initializes TLS; should only need to be called once.
void chpl_qsbr_init_tls(void);
void chpl_qsbr_init_tls(void) {
  struct tls_node *node = calloc(1, sizeof(struct tls_node));
  node->epoch = get_global_epoch();

  // Append to head of list
  struct tls_node *old_head;
  do {
    old_head = tls_list;
    node->next = old_head;
  } while (!atomic_compare_exchange_weak_uintptr_t(
      (uintptr_t *) &tls_list, (uintptr_t) old_head, (uintptr_t) node)
    );

  tls = node;
}

// Obtains the minimum epoch of all threads and of the disabled safest epoch.
static uint64_t safe_epoch(void);
static uint64_t safe_epoch(void) {
  uint64_t min = get_disabled_epoch();
  // Check all remaining threads for whether they have updated to a more recent epoch.
  for (struct tls_node *node = get_tls_list(); node != NULL; node = node->next) {
    if (!is_parked(node)) {
      uint64_t epoch = get_epoch(node);
      min = (min < epoch) ? min : epoch;       
    }
  } 
  
  return min;
}

// Requires spinlock.
static inline void handle_deferred_data(struct tls_node *node);
static inline void handle_deferred_data(struct tls_node *node) {
  #if TLS_CHECK
  if (atomic_load_uint_least64_t(&node->spinlock) != (uintptr_t) node) {
    error("Attempt to mutate data without holding spinlock...");
  }
  #endif

  // Acquire all data that can be deleted.
  uint64_t epoch = safe_epoch();
  struct defer_node *list = pop_bulk_defer_list(node, epoch);

  // Handle deletion of said data...
  // WARNING: Due to how `--memTrack` works, it is possible for
  // it to trigger a task-switch when it acquires the memTrack_sync
  // auxiliary sync var so we must handle this outside of the spinlock.
  release_spinlock(node);
  for (struct defer_node *dnode = list; dnode != NULL;) {
    struct defer_node *tmp = dnode;
    dnode = dnode->next;
    delete_data(tmp);
    push_recycle_list(node, tmp);
  }
  acquire_spinlock(node, (uintptr_t) node);
}

// Requires spinlock for both tls_nodes. Transfers deferred deletion from giver to receiver
static inline void give_deferred(struct tls_node *giver, struct tls_node *receiver);
static inline void give_deferred(struct tls_node *giver, struct tls_node *receiver) {
  #if TLS_CHECK
  if (atomic_load_uint_least64_t(&giver->spinlock) != (uintptr_t) giver
    || atomic_load_uint_least64_t(&receiver->spinlock) != (uintptr_t) giver) {
    error("Attempt to transfer data without holding both spinlocks...");
  }
  #endif
  struct defer_node *list = pop_all_defer_list(giver);
  
  if (receiver->deferList == NULL) {
    receiver->deferList = list;
  } else {
    // Merge both lists in order of decreasing targetEpoch
    struct defer_node *newHead = NULL;
    struct defer_node *newTail = NULL;
    while (list != NULL && receiver->deferList != NULL) {
      struct defer_node *dnode;
      if (list->targetEpoch >= receiver->deferList->targetEpoch) {
        dnode = list;
        list = list->next;
      } else {
        dnode = receiver->deferList;
        receiver->deferList = receiver->deferList->next;
      }

      if (newHead == NULL) {
        newHead = newTail = dnode;
        dnode->next = NULL;
      } else {
        newTail->next = dnode;
        newTail = dnode;
        dnode->next = NULL;
      }
    }
    // Append extra
    if (list != NULL) {
      newTail->next = list;
      list = NULL;
    } else if (receiver->deferList != NULL) {
      newTail->next = receiver->deferList;
      receiver->deferList = NULL;
    }

    #if TLS_CHECK
    {
      struct defer_node *prev = NULL;
      struct defer_node *curr = newHead;

      while (curr != NULL) {
        if (prev != NULL && prev->targetEpoch > curr->targetEpoch) {
          error("Merged list is not in sorted order!");
        }
        prev = curr;
        curr = curr->next;
      }
    }
    #endif
    receiver->deferList = newHead;
  }
}

void gc_init(void *heap, size_t sz, size_t blockSize) {
  persistent_heap_base = heap;
  persistent_heap_size = sz;
  for (size_t curr_sz = sz; curr_sz >= blockSize; curr_sz -= blockSize) {
    struct free_node *node = calloc(1, blockSize);
    node->next = free_list;
    free_list = node;
  }
}

void gc_checkpoint(void) {    
    // Initialize TLS...
    if (tls == NULL) {
      init_tls();
    }

    // Observe the current epoch.
    observe_epoch(tls);

    // Check if we have passed enough checkpoints to process our deferred data.
    if (tls->deferList != NULL) {
      acquire_spinlock(tls, (uintptr_t) tls);
      handle_deferred_data(tls);
      release_spinlock(tls);
    } 
}

static void gc_defer_deletion(void *data) {
  // Setup TLS if needed
  if (tls == NULL) {
    init_tls();
  }

  // Broadcast our state change
  uint64_t epoch = advance_global_epoch();
  set_epoch(tls, epoch);

  // Defer deletion of the data until all threads are
  // at least at the received epoch.
  struct defer_node *dnode = pop_recycle_list(tls);
  dnode->targetEpoch = epoch;
  dnode->data = data;

  // Defer deletion of our node.
  acquire_spinlock(tls, (uintptr_t) tls);
  push_defer_list(tls, dnode);
  release_spinlock(tls);
}

void chpl_qsbr_exit(void) {
    // Clean thread-local storage
    while (tls_list) {
        struct tls_node *node = tls_list;
        tls_list = tls_list->next;

        while (node->deferList) {
            struct defer_node *dnode = node->deferList;
            node->deferList = node->deferList->next;
            free(dnode);
        }

        while (node->recycleList) {
            struct defer_node *dnode = node->recycleList;
            node->recycleList = node->recycleList->next;
            chpl_mem_free(dnode, 0, 0);
        }

        chpl_mem_free(node, 0, 0);
    }
}