/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <linux/membarrier.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "jassert.h"
#include "lower_half_ckpt.h"

__thread LowerHalfThread lh_thread ATTR_TLS_INITIAL_EXEC;
bool lower_half_closed = false;

// Every thread that has used LOWER_HALF_DISABLE_CKPT().
static LowerHalfThread *threads = NULL;
static pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;

// The checkpoint thread while the lower half is closed.  It may still call
// the MPI wrappers itself.  Accessed atomically (relaxed): other threads read
// it, but only compare it with their own address.
static LowerHalfThread *closing_thread = NULL;

void
register_lower_half_thread()
{
  pthread_mutex_lock(&threads_lock);
  lh_thread.prev = NULL;
  lh_thread.next = threads;
  if (threads != NULL) {
    threads->prev = &lh_thread;
  }
  threads = &lh_thread;
  lh_thread.registered = true;
  pthread_mutex_unlock(&threads_lock);
}

// Called by an exiting thread (DMTCP_EVENT_PTHREAD_EXIT) before its TLS is
// freed.
void
unregister_lower_half_thread()
{
  if (!lh_thread.registered) {
    return;
  }
  pthread_mutex_lock(&threads_lock);
  if (lh_thread.prev != NULL) {
    lh_thread.prev->next = lh_thread.next;
  } else {
    threads = lh_thread.next;
  }
  if (lh_thread.next != NULL) {
    lh_thread.next->prev = lh_thread.prev;
  }
  lh_thread.registered = false;
  pthread_mutex_unlock(&threads_lock);
}

// Slow path of LOWER_HALF_DISABLE_CKPT() while the lower half is closed.
void
wait_until_lower_half_open()
{
  // Don't block a nested entry (the checkpoint thread is waiting for this
  // thread to leave) or the checkpoint thread itself.
  if (lh_thread.depth > 1 ||
      __atomic_load_n(&closing_thread, __ATOMIC_RELAXED) == &lh_thread) {
    return;
  }
  while (__atomic_load_n(&lower_half_closed, __ATOMIC_ACQUIRE)) {
    // Step back out, so that the checkpoint thread stops waiting for us and
    // DMTCP suspends this thread here, in the upper half.
    __atomic_store_n(&lh_thread.depth, 0, __ATOMIC_RELEASE);
    while (__atomic_load_n(&lower_half_closed, __ATOMIC_ACQUIRE)) {
      usleep(100);
    }
    __atomic_store_n(&lh_thread.depth, 1, __ATOMIC_RELAXED);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
  }
}

// Makes every running thread pass through a full memory barrier
// (membarrier(2)).  This is why LOWER_HALF_DISABLE_CKPT() needs no memory
// barrier of its own.
static void
make_lower_half_entries_visible()
{
  JASSERT(syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0, 0) == 0)
    (JASSERT_ERRNO);
}

// Fails at launch, rather than at the first checkpoint, without membarrier(2).
void
init_lower_half_ckpt()
{
  long commands = syscall(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0, 0);
  JASSERT(commands >= 0 && (commands & MEMBARRIER_CMD_GLOBAL))(commands)
    .Text("MANA needs membarrier(2) (Linux 4.3 or later)");
}

static void
close_lower_half_and_wait(bool skip_blocked)
{
  __atomic_store_n(&closing_thread, &lh_thread, __ATOMIC_RELAXED);
  __atomic_store_n(&lower_half_closed, true, __ATOMIC_RELAXED);
  // Now either a thread's LOWER_HALF_DISABLE_CKPT() sees lower_half_closed,
  // or we see its 'depth'.
  make_lower_half_entries_visible();
  pthread_mutex_lock(&threads_lock);
  for (LowerHalfThread *t = threads; t != NULL; t = t->next) {
    if (t == &lh_thread) {
      continue;
    }
    while (__atomic_load_n(&t->depth, __ATOMIC_ACQUIRE) > 0 &&
           !(skip_blocked &&
             __atomic_load_n(&t->in_blocking_call, __ATOMIC_ACQUIRE))) {
      usleep(100);
    }
  }
  pthread_mutex_unlock(&threads_lock);
}

void
wait_for_threads_to_leave_lower_half()
{
  close_lower_half_and_wait(false);
}

// Closes the lower half too, but does not wait for a thread that is blocked
// in a lower-half call: it may stay in MPI while the checkpoint thread
// drains (MANA_P2P_WAIT=blocking).  Any other thread leaves, and new entries
// wait, so that the checkpoint thread is the only one that tests requests.
void
wait_for_unblocked_threads_to_leave_lower_half()
{
  close_lower_half_and_wait(true);
}

void
allow_threads_to_enter_lower_half()
{
  __atomic_store_n(&closing_thread, (LowerHalfThread*)NULL, __ATOMIC_RELAXED);
  __atomic_store_n(&lower_half_closed, false, __ATOMIC_RELEASE);
}
