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
#include <unistd.h>

/* Defined in asm/hwcap.h */
#ifndef HWCAP2_FSGSBASE
#define HWCAP2_FSGSBASE        (1 << 1)
#endif

#define ENV_VAR_FSGSBASE_ENABLED        "DMTCP_FSGSBASE_ENABLED"

extern int FsGsBaseEnabled;
int CheckAndEnableFsGsBase();
void setFS(unsigned long fsbase);
unsigned long getFS(void);

// Helper macros to be used whenever the upper half calls into the lower
// half (for example, a wrapper calling the real function), and returns:
//   JUMP_TO_LOWER_HALF(lh_info->fsaddr);
//   retval = NEXT_FUNC(Send)(...);
//   RETURN_TO_UPPER_HALF();
// The first saves the FS register of the calling thread and sets the lower
// half's; the second restores it.  (A lower half FS of 0 switches nothing.)
// Nothing between them may leave the block (return, goto, break, continue,
// or an exception): the FS register would not be restored.
#define JUMP_TO_LOWER_HALF(lhFs) \
  do { \
    unsigned long lhFs_ = (unsigned long)(lhFs); \
    unsigned long uhFs_ = 0; \
    if (lhFs_ != 0) { \
      uhFs_ = getFS(); \
      setFS(lhFs_); \
    }

#define RETURN_TO_UPPER_HALF() \
    if (lhFs_ != 0) { \
      setFS(uhFs_); \
    } \
  } while (0)

#define ONEMB (uint64_t)(1024 * 1024)
#define ONEGB (uint64_t)(1024 * 1024 * 1024)

// Rounds the given address up/down to nearest region size, given as an input.
//   (similar to define's in lower-half/mmap_internal.h)
#define PAGE_SIZE              0x1000
#define HUGE_PAGE              0x200000
#define ROUND_UP(addr, size) (((unsigned long)(addr) + size - 1) & ~(size - 1))
#define ROUND_DOWN(addr, size) ((unsigned long)(addr) & ~(size - 1))

#ifdef __clang__
# define NO_OPTIMIZE __attribute__((optnone))
#else /* ifdef __clang__ */
# define NO_OPTIMIZE __attribute__((optimize(0)))
#endif /* ifdef __clang__ */

#endif // ifndef _SPLIT_PROCESS_H
