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

#ifndef _LOWER_HALF_CKPT_H
#define _LOWER_HALF_CKPT_H

#include "dmtcp.h"

// Keeps checkpoints out of the lower half, which is discarded at checkpoint.
// MPI wrappers bracket lower-half calls, and the upper-half state that goes
// with them (virtual IDs, the pending P2P log), with LOWER_HALF_DISABLE_CKPT()
// and LOWER_HALF_ENABLE_CKPT(); the pairs may nest.  In pre-suspend, the
// checkpoint thread blocks new entries and waits until no thread is inside
// (wait_for_threads_to_leave_lower_half()).  After DMTCP has suspended the
// threads, it lets them in again (allow_threads_to_enter_lower_half()).

struct LowerHalfThread {
  // Nesting depth in LOWER_HALF_DISABLE_CKPT().  Only this thread writes it;
  // the checkpoint thread reads it.
  int depth;
  bool registered;
  LowerHalfThread *prev;
  LowerHalfThread *next;
};

extern __thread LowerHalfThread lh_thread ATTR_TLS_INITIAL_EXEC;
extern bool lower_half_closed;

void init_lower_half_ckpt();
void register_lower_half_thread();
void unregister_lower_half_thread();
void wait_until_lower_half_open();
void wait_for_threads_to_leave_lower_half();
void allow_threads_to_enter_lower_half();

#define LOWER_HALF_DISABLE_CKPT() lower_half_disable_ckpt()
#define LOWER_HALF_ENABLE_CKPT() lower_half_enable_ckpt()

static inline void
lower_half_disable_ckpt()
{
  if (__builtin_expect(!lh_thread.registered, 0)) {
    register_lower_half_thread();
  }
  __atomic_store_n(&lh_thread.depth, lh_thread.depth + 1, __ATOMIC_RELAXED);
  // A compiler barrier suffices between the store and the load: the
  // checkpoint thread's membarrier() supplies the memory ordering.
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
  if (__builtin_expect(__atomic_load_n(&lower_half_closed, __ATOMIC_RELAXED),
                       0)) {
    wait_until_lower_half_open();
  }
}

static inline void
lower_half_enable_ckpt()
{
  __atomic_store_n(&lh_thread.depth, lh_thread.depth - 1, __ATOMIC_RELEASE);
}

#endif  // ifndef _LOWER_HALF_CKPT_H
