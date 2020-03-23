
/*--------------------------------------------------------------------*/
/*--- Linux random ticket lock implementation        random-lock.c ---*/
/*---                                                              ---*/
/*--- Adaptation of ticket-lock-linux.c where a random thread will ---*/
/*--- chosen, rather than being round-robin; useful for improving  ---*/
/*--- the chance of finding data-racy bugs.                        ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2011-2017 Bart Van Assche <bvanassche@acm.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_core_basics.h"
#include "pub_core_libcassert.h"
#include "pub_core_libcbase.h"     // VG_(memset)()
#include "pub_core_libcprint.h"
#include "pub_core_syscall.h"
#include "pub_core_vki.h"
#include "pub_core_vkiscnums.h"    // __NR_futex
#include "pub_core_libcproc.h"
#include "pub_core_mallocfree.h"
#include "pub_core_threadstate.h"
#include "pub_core_inner.h"
#if defined(ENABLE_INNER_CLIENT_REQUEST)
#include "helgrind/helgrind.h"
#endif
#include "priv_sched-lock.h"
#include "priv_sched-lock-impl.h"
#include "pub_core_clientstate.h"

struct semaphore {
   volatile unsigned count; 
};

void semaphore_init(struct semaphore *sema) {
   sema->count = 0;
   INNER_REQUEST(ANNOTATE_BENIGN_RACE_SIZED(&sema->count, sizeof(sema->count), ""));
}

void semaphore_deinit(struct semaphore *sema) {}

/* get a token */
void semaphore_await(struct semaphore *sema) {
   while (True) {
      __sync_synchronize();
      if (sema->count == 1) break;
      SysRes sres = VG_(do_syscall3)(__NR_futex, (UWord) &sema->count, VKI_FUTEX_WAIT | VKI_FUTEX_PRIVATE_FLAG, 0);
      if (sr_isError(sres) && sr_Err(sres) != VKI_EAGAIN) {
         VG_(printf)("futex_wait() returned error code %lu\n", sr_Err(sres));
         vg_assert(False);
      }
   }
}

void semaphore_reset(struct semaphore *sema) {
   sema->count = 0;
   __sync_synchronize();
}

/* put token back */
void semaphore_signal(struct semaphore *sema) {
   sema->count = 1;
   __sync_synchronize();
   SysRes sres = VG_(do_syscall3)(__NR_futex, (UWord)&sema->count,
                              VKI_FUTEX_WAKE | VKI_FUTEX_PRIVATE_FLAG,
                              0x7fffffff);
   vg_assert(!sr_isError(sres));
}


struct semaphore_wrapper {
   struct semaphore sema;
   volatile ThreadId tid;
};

// Scheduler lock must be acquired first prior to doing anything, including
// adding yourself to the list of threads for waiting, and for acquiring the
// scheduler lock.
struct sched_lock {
   int sched_lock;
   volatile SizeT num_thread_locks;
   volatile SizeT used_thread_locks;
   volatile struct semaphore_wrapper **thread_locks;
   volatile ThreadId owner;
};

extern UInt VG_(quantum_seed);

static void acquire(struct sched_lock *lock) {
   if (__sync_lock_test_and_set(&lock->sched_lock, 1) == 0) {
      return;
   }

   while (True) {
      __sync_synchronize();
      if (lock->sched_lock == 0 && __sync_lock_test_and_set(&lock->sched_lock, 1) == 0) {
         return;
      }
   }
}

static void release(struct sched_lock *lock) {
   lock->sched_lock = 0;
   __sync_synchronize();
}

static const HChar *get_sched_lock_name(void)
{
   return "random lock";
}

static struct sched_lock *create_sched_lock(void)
{
   struct sched_lock *p;

   p = VG_(malloc)("sched_lock", sizeof(*p));

   ML_(sema_init)(&p->sched_lock);
   p->owner = VG_INVALID_THREADID;
   p->thread_locks = NULL;
   p->num_thread_locks = 0;
   p->used_thread_locks = 0;
   p->sched_lock = 0;

   INNER_REQUEST(ANNOTATE_RWLOCK_CREATE(p));
   VG_(emit)("Hard Limit is %d\n", VG_(fd_hard_limit));
   return p;
}

static void destroy_sched_lock(struct sched_lock *p)
{
   INNER_REQUEST(ANNOTATE_RWLOCK_DESTROY(p));
   ML_(sema_deinit)(&p->sched_lock);
   for (int i = 0; i < p->num_thread_locks; i++) {
      semaphore_deinit(&p->thread_locks[i]->sema);
      VG_(free)(p->thread_locks[i]);
   }
   VG_(free)(p->thread_locks);
   VG_(free)(p);
}

static int get_sched_lock_owner(struct sched_lock *p) {
   return p->owner;
}

static void release_sched_lock(struct sched_lock *p) {
   acquire(p);
   // Check if anyone needs access
   if (p->used_thread_locks) {
      Int iterations = VG_(random)(&VG_(quantum_seed)) % p->used_thread_locks;
      for (int i = 0; i < p->num_thread_locks; i++) {
         if (iterations == 0 && p->thread_locks[i] && p->thread_locks[i]->tid != VG_INVALID_THREADID) {
            p->owner = p->thread_locks[i]->tid;
            semaphore_signal(&p->thread_locks[i]->sema);
            break;
         }
         if (p->thread_locks[i] && p->thread_locks[i]->tid != VG_INVALID_THREADID) {
            iterations--;
         }
      }
   } else {
      // No one needs it, just relinquish...
      p->owner = VG_INVALID_THREADID;
   }
   release(p);
}

static void acquire_sched_lock(struct sched_lock *p) {
   acquire(p);
   // If some other thread owns the lock, add self to wait list.
   if (p->owner != VG_INVALID_THREADID) {
      // Full? Extend!
      int idx;
      if (p->num_thread_locks == p->used_thread_locks) {
         p->num_thread_locks = VG_MAX(1, p->num_thread_locks * 2);
         struct sched_lock **tmp = VG_(malloc)("sched_lock->thread_locks", p->num_thread_locks * sizeof(struct sched_lock *));
         for (int i = 0; i < p->used_thread_locks; i++) {
            tmp[i] = p->thread_locks[i];
         }
         for (int i = p->used_thread_locks; i < p->num_thread_locks; i++) {
            tmp[i] = NULL;
         }
         VG_(free)(p->thread_locks);
         p->thread_locks = tmp;
         
         idx = p->used_thread_locks;
         p->thread_locks[idx] = VG_(malloc)("sema_wrapper", sizeof(struct semaphore_wrapper));
         semaphore_init(&p->thread_locks[idx]->sema);
      } else {
         // Otherwise scan for empty slot
         for (int i = 0; i < p->num_thread_locks; i++) {
            if (!p->thread_locks[i]) {
               idx = i;
               p->thread_locks[i] = VG_(malloc)("sema_wrapper", sizeof(struct semaphore_wrapper));
               semaphore_init(&p->thread_locks[i]->sema);
               break;
            }
            if (p->thread_locks[i]->tid == VG_INVALID_THREADID) {
               idx = i;
               break;
            }
         }
      }

      // Claim for our own
      int tid = VG_(gettid)();
      p->thread_locks[idx]->tid = tid;
      p->used_thread_locks++;
      semaphore_reset(&p->thread_locks[idx]->sema);
      release(p);
      semaphore_await(&p->thread_locks[idx]->sema);
      acquire(p);
      p->thread_locks[idx]->tid = VG_INVALID_THREADID;
      p->used_thread_locks--;
      tl_assert2(p->owner == tid, "The owner of scheduler lock is not (%ld,%ld) but is instead %ld!\n", VG_(gettid)(), tid,  p->owner);
      release(p);
   } else {
      p->owner = VG_(gettid)();
      release(p);
      return;
   }
}

const struct sched_lock_ops ML_(random_lock_ops) = {
   .get_sched_lock_name  = get_sched_lock_name,
   .create_sched_lock    = create_sched_lock,
   .destroy_sched_lock   = destroy_sched_lock,
   .get_sched_lock_owner = get_sched_lock_owner,
   .acquire_sched_lock   = acquire_sched_lock,
   .release_sched_lock   = release_sched_lock,
};
