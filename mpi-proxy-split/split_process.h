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

#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H
#include <asm/prctl.h>
#include <linux/version.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include "jassert.h"

/* Defined in asm/hwcap.h */
#ifndef HWCAP2_FSGSBASE
#define HWCAP2_FSGSBASE        (1 << 1)
#endif

extern bool FsGsBaseEnabled;
bool CheckAndEnableFsGsBase();

/* The support to set and get FS base register in user-space has been merged in
 * Linux kernel v5.3 (see https://elixir.bootlin.com/linux/v5.3/C/ident/rdfsbase
 *  or https://www.phoronix.com/scan.php?page=news_item&px=Linux-5.3-FSGSBASE).
 *
 * MANA leverages this faster user-space switch on kernel version >= 5.3.
 */
static inline unsigned long getFS(void)
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
    JWARNING(syscall(SYS_arch_prctl, ARCH_GET_FS, &fsbase) == 0) (JASSERT_ERRNO);
  }
  return fsbase;
}

static inline void setFS(unsigned long fsbase)
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
    JWARNING(syscall(SYS_arch_prctl, ARCH_SET_FS, fsbase) == 0) (JASSERT_ERRNO);
  }
}

// Helper class to save and restore context (in particular, the FS register),
// when switching between the upper half and the lower half. In the current
// design, the caller would generally be the upper half, trying to jump into
// the lower half. An example would be calling a real function defined in the
// lower half from a function wrapper defined in the upper half.
// Example usage:
//   int function_wrapper()
//   {
//     SwitchContext ctx;
//     return _real_function();
//   }
// The idea is to leverage the C++ language semantics to help us automatically
// restore the context when the object goes out of scope.
class SwitchContext
{
  private:
    unsigned long upperHalfFs; // The base value of the FS register of the upper half thread
    unsigned long lowerHalfFs; // The base value of the FS register of the lower half

  public:
    // Saves the current FS register value to 'upperHalfFs' and then
    // changes the value of the FS register to the given 'lowerHalfFs'
    explicit SwitchContext(unsigned long );

    // Restores the FS register value to 'upperHalfFs'
    ~SwitchContext();
};

// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs) \
  do { \
    SwitchContext ctx((unsigned long)lhFs)

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF() \
  } while (0)

#define ONEMB (uint64_t)(1024 * 1024)
#define ONEGB (uint64_t)(1024 * 1024 * 1024)

// Rounds the given address up/down to nearest region size, given as an input.
//   (similar to define's in lower-half/mmap_internal.h)
#define PAGE_SIZE              0x1000
#define HUGE_PAGE              0x200000
#define ROUND_UP(addr, size) (((unsigned long)addr + size - 1) & ~(size - 1))
#define ROUND_DOWN(addr, size) ((unsigned long)addr & ~(size - 1))

#ifdef __clang__
# define NO_OPTIMIZE __attribute__((optnone))
#else /* ifdef __clang__ */
# define NO_OPTIMIZE __attribute__((optimize(0)))
#endif /* ifdef __clang__ */

// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();
extern void updateVdsoLinkmapEntry(void *);
extern void *getUhVdsoLdAddr();

#endif // ifndef _SPLIT_PROCESS_H
