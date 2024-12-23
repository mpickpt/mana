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

// Needed for process_vm_readv
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <linux/version.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <libgen.h>
#include <limits.h>
#include <link.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <assert.h>


#include "switch-context.h"
#include "lower-half-api.h"

bool FsGsBaseEnabled = false;

bool CheckAndEnableFsGsBase()
{
  pid_t childPid = fork();
  assert(childPid != -1);

  if (childPid == 0) {
    unsigned long fsbase = -1;
    // On systems without FSGSBASE support (Linux kernel < 5.9, this instruction
    // fails with SIGILL).
    asm volatile("rex.W\n rdfsbase %0" : "=r" (fsbase) :: "memory");
    if (fsbase != (unsigned long)-1) {
      exit(0);
    }

    // Also test wrfsbase in case it generates SIGILL as well.
    asm volatile("rex.W\n wrfsbase %0" :: "r" (fsbase) : "memory");
    exit(1);
  }

  int status = 0;
  assert(waitpid(childPid, &status, 0) == childPid);

  if (status == 0) {
    FsGsBaseEnabled = true;
  } else {
    FsGsBaseEnabled = false;
  }
  return FsGsBaseEnabled;
}

/* The support to set and get FS base register in user-space has been merged in
 * Linux kernel v5.3 (see https://elixir.bootlin.com/linux/v5.3/C/ident/rdfsbase
 *  or https://www.phoronix.com/scan.php?page=news_item&px=Linux-5.3-FSGSBASE).
 *
 * MANA leverages this faster user-space switch on kernel version >= 5.3.
 */
unsigned long getFS(void)
{
  unsigned long fsbase;

  if (FsGsBaseEnabled) {
    // This user-space variant is equivalent, but faster.
    // Optionally, this->upperHalfFs could be cached if MPI_THREAD_MULTIPLE
    //   was not specified, but this should already be fast.
    // #if:  Linux kernel 5.9 or higher guarantees that FSGSBASE is supported.
    // For now, include "defined(HAS_FSGSBASE)" to see if FSGSBASE was backported.

    // The prefix 'rex.W' is required or 'rdfsbase' will assume 32 bits.
    asm volatile("rex.W\n rdfsbase %0" : "=r" (fsbase) :: "memory");
  } else {
    syscall(SYS_arch_prctl, ARCH_GET_FS, &fsbase);
  }
  return fsbase;
}

void setFS(unsigned long fsbase)
{
  if (FsGsBaseEnabled) {
    // This user-space variant is equivalent, but faster.
    // Optionally, this->upperHalfFs could be cached if MPI_THREAD_MULTIPLE
    //   was not specified, but this should already be fast.
    // #if:  Linux kernel 5.9 or higher guarantees that FSGSBASE is supported.
    // For now, include "defined(HAS_FSGSBASE)" to see if FSGSBASE was backported.

    // The prefix 'rex.W' is required or 'rdfsbase' will assume 32 bits.
    asm volatile("rex.W\n wrfsbase %0" :: "r" (fsbase) : "memory");
  } else {
    syscall(SYS_arch_prctl, ARCH_SET_FS, fsbase);
  }
}


SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
  jumpped = 0;
  if (lowerHalfFs > 0) {
    this->lowerHalfFs = lowerHalfFs;
    this->upperHalfFs = getFS();
    setFS(this->lowerHalfFs);
    jumpped = 1;
  }
}

SwitchContext::~SwitchContext()
{
  if (jumpped) {
    setFS(this->upperHalfFs);
    jumpped = 0;
  }
}
