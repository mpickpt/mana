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

// Disabling checkpointing while a thread executes in the lower half.
//
// A checkpoint must not happen while a thread runs lower-half code: the
// lower half is discarded at checkpoint and rebuilt at restart.  An MPI
// wrapper brackets its calls into the lower half, together with the
// upper-half state that goes with them (virtual IDs, the pending P2P log),
// with
//   LOWER_HALF_DISABLE_CKPT();
//   ...
//   LOWER_HALF_ENABLE_CKPT();
// The pairs may nest.  At the end of the pre-suspend phase, the checkpoint
// thread calls wait_for_threads_to_leave_lower_half(): from then on, a
// thread that reaches LOWER_HALF_DISABLE_CKPT() waits there, and the call
// returns once no thread is between the two.  After DMTCP has suspended the
// threads, the checkpoint thread calls allow_threads_to_enter_lower_half().
//
// This does for the MPI wrappers what DMTCP's wrapper lock
// (DMTCP_PLUGIN_DISABLE_CKPT) does, but more cheaply: a thread updates only
// its own counter, with no atomic instruction.  The checkpoint thread pays
// for the memory ordering instead (see wait_for_threads_to_leave_lower_half()).

struct LowerHalfThread {
  // How deeply this thread is nested in LOWER_HALF_DISABLE_CKPT().  Only the
  // thread itself writes it; the checkpoint thread reads it.
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
  // Only a compiler barrier is needed between the store above and the load
  // below: wait_for_threads_to_leave_lower_half() makes the store visible to
  // the checkpoint thread before it reads 'depth'.
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
