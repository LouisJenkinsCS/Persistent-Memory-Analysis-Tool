
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

#define TL_FUTEX_COUNT_LOG2 4
#define TL_FUTEX_COUNT (1U << TL_FUTEX_COUNT_LOG2)
#define TL_FUTEX_MASK (TL_FUTEX_COUNT - 1)

struct sched_lock {
   volatile unsigned head;
   volatile unsigned tail;
   volatile unsigned futex[TL_FUTEX_COUNT];
   volatile unsigned seed;
   int owner;
};

#if 1
static Bool s_debug;
#else
static Bool s_debug = True;
#endif

extern UInt VG_(quantum_seed);

static const HChar *get_sched_lock_name(void)
{
   return "random ticket lock";
}

static struct sched_lock *create_sched_lock(void)
{
   struct sched_lock *p;

   p = VG_(malloc)("sched_lock", sizeof(*p));

   // The futex syscall requires that a futex takes four bytes.
   vg_assert(sizeof(p->futex[0]) == 4);

   VG_(memset)(p, 0, sizeof(*p));

   INNER_REQUEST(ANNOTATE_RWLOCK_CREATE(p));
   INNER_REQUEST(ANNOTATE_BENIGN_RACE_SIZED(&p->futex, sizeof(p->futex), ""));
   return p;
}

static void destroy_sched_lock(struct sched_lock *p)
{
   INNER_REQUEST(ANNOTATE_RWLOCK_DESTROY(p));
   VG_(free)(p);
}

static int get_sched_lock_owner(struct sched_lock *p)
{
   return p->owner;
}

/*
 * Acquire ticket lock. Increment the tail of the queue and use the original
 * value as the ticket value. Wait until the head of the queue equals the
 * ticket value. The futex used to wait depends on the ticket value in order
 * to avoid that all threads get woken up every time a ticket lock is
 * released. That last effect is sometimes called the "thundering herd"
 * effect.
 *
 * See also Nick Piggin, x86: FIFO ticket spinlocks, Linux kernel mailing list
 * (http://lkml.org/lkml/2007/11/1/125) for more info.
 */
static void _acquire_sched_lock(struct sched_lock *p)
{
   unsigned ticket, futex_value;
   volatile unsigned *futex;
   SysRes sres;

   ticket = __sync_fetch_and_add(&p->tail, 1);
   futex = &p->futex[ticket & TL_FUTEX_MASK];
   if (s_debug)
      VG_(printf)("[%d/%d] acquire: ticket %u\n", VG_(getpid)(),
                  VG_(gettid)(), ticket);
   for (;;) {
      futex_value = *futex;
      __sync_synchronize();
      if (ticket == p->head)
         break;
      if (s_debug)
         VG_(printf)("[%d/%d] acquire: ticket %u - waiting until"
                     " futex[%ld] != %u\n", VG_(getpid)(),
                     VG_(gettid)(), ticket, (long)(futex - p->futex),
                     futex_value);
      sres = VG_(do_syscall3)(__NR_futex, (UWord)futex,
                              VKI_FUTEX_WAIT | VKI_FUTEX_PRIVATE_FLAG,
                              futex_value);
      if (sr_isError(sres) && sr_Err(sres) != VKI_EAGAIN) {
         VG_(printf)("futex_wait() returned error code %lu\n", sr_Err(sres));
         vg_assert(False);
      }
   }
   __sync_synchronize();
   INNER_REQUEST(ANNOTATE_RWLOCK_ACQUIRED(p, /*is_w*/1));
   vg_assert(p->owner == 0);
   p->owner = VG_(gettid)();
}

/*
 * Release a ticket lock by incrementing the head of the queue. Only generate
 * a thread wakeup signal if at least one thread is waiting. If the queue tail
 * matches the wakeup_ticket value, no threads have to be woken up.
 *
 * Note: tail will only be read after head has been incremented since both are
 * declared as volatile and since the __sync...() functions imply a memory
 * barrier.
 */
static void release_sched_lock(struct sched_lock *p)
{
   unsigned wakeup_ticket, futex_value;
   volatile unsigned *futex;
   SysRes sres;

   vg_assert(p->owner != 0);
   p->owner = 0;
   INNER_REQUEST(ANNOTATE_RWLOCK_RELEASED(p, /*is_w*/1));
   wakeup_ticket = __sync_fetch_and_add(&p->head, 1) + 1;
   if (p->tail != wakeup_ticket) {
      futex = &p->futex[wakeup_ticket & TL_FUTEX_MASK];
      futex_value = __sync_fetch_and_add(futex, 1);
      if (s_debug)
         VG_(printf)("[%d/%d] release: waking up ticket %u (futex[%ld] = %u)"
                     "\n", VG_(getpid)(), VG_(gettid)(), wakeup_ticket,
                     (long)(futex - p->futex), futex_value);
      sres = VG_(do_syscall3)(__NR_futex, (UWord)futex,
                              VKI_FUTEX_WAKE | VKI_FUTEX_PRIVATE_FLAG,
                              0x7fffffff);
      vg_assert(!sr_isError(sres));
   } else {
      if (s_debug)
         VG_(printf)("[%d/%d] release: no thread is waiting for ticket %u\n",
                     VG_(getpid)(), VG_(gettid)(), wakeup_ticket);
   }
}

static void acquire_sched_lock(struct sched_lock *p) {
   while (True) {
      _acquire_sched_lock(p);
      unsigned n = __sync_fetch_and_add(&p->tail, 0) - __sync_fetch_and_add(&p->head, 0);
      if (VG_(random)(&VG_(quantum_seed)) % n == 0) {
         return;
      } 
      release_sched_lock(p);
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
