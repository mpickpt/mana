#/*****************************************************************************
# * Copyright (C) 2021 Jun Gan <jun.gan@memverge.com>                         *
# *                                                                           *
# * DMTCP is free software: you can redistribute it and/or                    *
# * modify it under the terms of the GNU Lesser General Public License as     *
# * published by the Free Software Foundation, either version 3 of the        *
# * License, or (at your option) any later version.                           *
# *                                                                           *
# * DMTCP is distributed in the hope that it will be useful,                  *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
# * GNU Lesser General Public License for more details.                       *
# *                                                                           *
# * You should have received a copy of the GNU Lesser General Public          *
# * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
# *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <execinfo.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <asm/processor-flags.h>
#include <assert.h>

#include "memory_access_tracker.h"

/* TODO: support multiple trackers and thread safe?
 */
// static LIST_HEAD(mem_trackers);
static struct MemTracker *mem_tracker;
/* Make it compatible with applications have their own SEGV handlers */
static struct sigaction old_act;
/* The one will be used in SIGTRAP handler */
static struct MemTracker *trap_tracker;
static bool segv_handler_installed = false;
/* number of instructions we need to bypass
 * Set by SEGV hander and used in TRAP handler */
static int num_instructions;

/* EFLAGS offset, see RT for details_ sigframe
 * 208 from stack top for SEGV, 200 for SIGTRAP
 * FIXME: depends what we do in the sighander, these offsets can change
 */
#define F_OFFSET_SEGV 208
#define F_OFFSET_TRAP 200

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif

static bool NoActiveTrackers() {
  return mem_tracker == NULL;
}

static struct MemTracker* GetMemoryTracker(void *addr) {
  /* TODO: Get from trackers list */
  if (mem_tracker
      && addr >= mem_tracker->addr
      && (uintptr_t)addr < (uintptr_t)mem_tracker->addr + mem_tracker->len) {
    return mem_tracker;
  }
  /* not tracked memory */
  return NULL;
}

static void TrapHandler(int signum) {
  /* used to calculate EFLAGs location */
  unsigned long *p;

  if (num_instructions-- > 0) {
    return;
  }

  if (trap_tracker) {
    // protect again before exit
    if (mprotect(trap_tracker->addr, trap_tracker->len, PROT_NONE)) {
      perror("mrpotect failed");
    }
  }

  // Unset TF
  p = (unsigned long*)((uintptr_t)&p + F_OFFSET_TRAP);
  *p &= ~X86_EFLAGS_TF;
  // signal(SIGTRAP, SIG_DFL);
}

static void AddTrapHandler() {
  if (signal(SIGTRAP, TrapHandler)) {
    perror("Failed to install trap handler");
  }
}

static void SegvfaultHandler(int signum, siginfo_t *siginfo, void *context) {
  /* used to calculate EFLAGs location */
  unsigned long *p;
#if 0
#define FRAME_SIZE 100
  void *buffer[FRAME_SIZE];
  int n;
#endif

  struct MemTracker *tracker = GetMemoryTracker(siginfo->si_addr);
  if (tracker == NULL) {
    /* not belong to any trackers, call old handler */
    if (old_act.sa_flags & SA_SIGINFO) {
      old_act.sa_sigaction(signum, siginfo, context);
    } else {
      if (old_act.sa_handler == SIG_IGN) return;
      if (old_act.sa_handler == SIG_DFL) {
	signal(SIGSEGV, SIG_DFL);
      } else {
	old_act.sa_handler(signum);
      }
    }
    return;
  }

  /* TODO: I wanted to print stack trace but found the TF won't work if I did this
   * Probably becasue the EFLAGs offset changed */
#if 0
  if (tracker->fd >= 0) {
    // Save stack trace
    n = backtrace(buffer, FRAME_SIZE);
    backtrace_symbols_fd(buffer, n, tracker->fd); 
  }
#endif

  if (tracker->num_access < tracker->max_num) {
    tracker->addrs[tracker->num_access++] = siginfo->si_addr;
  }

  // Re-enalbe read write
  // FIXME: only for 1 page instead of whole region?
  if (mprotect(tracker->addr, tracker->len, PROT_READ|PROT_WRITE)) {
    perror("mrpotect failed");
    return;
  }

  num_instructions = 1;

  // Set which will take effect in trap
  trap_tracker = tracker;

  // Cannot add TRAP handler here
  // AddTrapHandler();

  // Set TF bit which will take effect after sigreturn
  p = (unsigned long*)((uintptr_t)&p + F_OFFSET_SEGV);
  *p = *p | X86_EFLAGS_TF;
}

static int AddSegvHandler() {
  struct sigaction act;

  act.sa_sigaction = &SegvfaultHandler;
  act.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGSEGV, &act, &old_act)) {
    perror("Failed to install segv handler");
    return -1;
  }
  return 0;
}

static void RemoveSegvHandler() {
  if (old_act.sa_flags & SA_SIGINFO) {
    sigaction(SIGSEGV, &old_act, NULL);
  } else {
    signal(SIGSEGV, old_act.sa_handler);
  }
}

struct MemTracker* StartTrackMemory(void *addr, size_t len, uint32_t max_num, int log_fd) {
  long pagesize;

  pagesize = sysconf(_SC_PAGE_SIZE);
  if (pagesize < 1) {
    perror("Invalid pagesize");
    pagesize = 4096;
  }

  struct MemTracker *tracker = malloc(sizeof(struct MemTracker));
  if (tracker == NULL) {
    perror("malloc failed");
    return NULL;
  }

  // Has to be page aligned
  tracker->addr = (void *)((uintptr_t)addr & ~(pagesize - 1));
  tracker->len = roundup((uintptr_t)addr + len, pagesize) - (uintptr_t)tracker->addr;
  tracker->fd = log_fd;
  if (max_num) {
    /* Pre allocate since we cannot call alloc in sighandler */
    tracker->addrs = calloc(max_num, sizeof(void *));
    if (tracker->addrs == NULL) {
      perror("malloc failed");
      tracker->max_num= 0;
    } else {
      tracker->max_num = max_num;
    }
  }
  tracker->num_access = 0;

  // TODO: add tracker to the list
  assert(!mem_tracker);
  mem_tracker = tracker;
  
  if (!segv_handler_installed) {
    if (AddSegvHandler() != 0) {
      goto error;
    }
    AddTrapHandler();
    segv_handler_installed = true;
  }

  // disalbe read/write access
  if (mprotect(tracker->addr, tracker->len, PROT_NONE)) {
    perror("mprotect failed");
    goto error;
  }
  
  return tracker;

error:
  EndTrackMemory(tracker);
  FreeMemTracker(tracker);
  return NULL;
}

void EndTrackMemory(struct MemTracker *tracker) {
  if (mprotect(tracker->addr, tracker->len, PROT_READ | PROT_WRITE)) {
    perror("mprotect failed");
  }

  /* TODO: remove tracker from list */
  mem_tracker = NULL;

  if (NoActiveTrackers()) {
    RemoveSegvHandler();
    signal(SIGTRAP, SIG_DFL);
    segv_handler_installed = false;
  }
}

void FreeMemTracker(struct MemTracker *tracker) {
  free(tracker->addrs);
  memset(tracker, 0, sizeof(*tracker));
  free(tracker);
  tracker = NULL;
}

