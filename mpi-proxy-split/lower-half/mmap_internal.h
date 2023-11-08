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

/* Common mmap definition for Linux implementation.
   Copyright (C) 2017-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef MMAP_INTERNAL_LINUX_H
#define MMAP_INTERNAL_LINUX_H 1

/* This is the minimum mmap2 unit size accept by the kernel.  An architecture
   with multiple minimum page sizes (such as m68k) might define it as -1 and
   thus it will queried at runtime.  */
#ifndef MMAP2_PAGE_UNIT
# define MMAP2_PAGE_UNIT 4096ULL
#endif

#if MMAP2_PAGE_UNIT == -1
static uint64_t page_unit;
# define MMAP_CHECK_PAGE_UNIT()                                                \
  if (page_unit == 0)                                                          \
    page_unit = __getpagesize ();
# undef MMAP2_PAGE_UNIT
# define MMAP2_PAGE_UNIT page_unit
#else
# define MMAP_CHECK_PAGE_UNIT()
#endif

// TODO: Add definitions for other architectures?

/* Do not accept offset not multiple of page size.  */
#define MMAP_OFF_LOW_MASK  (MMAP2_PAGE_UNIT - 1)

/* Registers clobbered by syscall.  */
# define REGISTERS_CLOBBERED_BY_SYSCALL "cc", "r11", "cx"

/* Create a variable 'name' based on type 'X' to avoid explicit types.
   This is mainly used set use 64-bits arguments in x32.   */
#define TYPEFY(X, name) __typeof__ ((X) - (X)) name
/* Explicit cast the argument to avoid integer from pointer warning on
   x32.  */
#define ARGIFY(X) ((__typeof__ ((X) - (X))) (X))
#define SYS_ify(syscall_name) __NR_##syscall_name

#define internal_syscall2(number, err, arg1, arg2)      \
({                  \
    unsigned long int resultvar;          \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);        \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);        \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;     \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;     \
    asm volatile (              \
    "syscall\n\t"             \
    : "=a" (resultvar)              \
    : "0" (number), "r" (_a1), "r" (_a2)        \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);      \
    (long int) resultvar;           \
})

#define internal_syscall3(number, err, arg1, arg2, arg3)    \
({                  \
    unsigned long int resultvar;          \
    TYPEFY (arg3, __arg3) = ARGIFY (arg3);        \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);        \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);        \
    register TYPEFY (arg3, _a3) asm ("rdx") = __arg3;     \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;     \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;     \
    asm volatile (              \
    "syscall\n\t"             \
    : "=a" (resultvar)              \
    : "0" (number), "r" (_a1), "r" (_a2), "r" (_a3)     \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);      \
    (long int) resultvar;           \
})

#define internal_syscall6(number, err, arg1, arg2, arg3, arg4, arg5, arg6)     \
({                                                                             \
    unsigned long int resultvar;                                               \
    TYPEFY (arg6, __arg6) = ARGIFY (arg6);                                     \
    TYPEFY (arg5, __arg5) = ARGIFY (arg5);                                     \
    TYPEFY (arg4, __arg4) = ARGIFY (arg4);                                     \
    TYPEFY (arg3, __arg3) = ARGIFY (arg3);                                     \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);                                     \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);                                     \
    register TYPEFY (arg6, _a6) asm ("r9") = __arg6;                           \
    register TYPEFY (arg5, _a5) asm ("r8") = __arg5;                           \
    register TYPEFY (arg4, _a4) asm ("r10") = __arg4;                          \
    register TYPEFY (arg3, _a3) asm ("rdx") = __arg3;                          \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;                          \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;                          \
    asm volatile (                                                             \
    "syscall\n\t"                                                              \
    : "=a" (resultvar)                                                         \
    : "0" (number), "r" (_a1), "r" (_a2), "r" (_a3), "r" (_a4),                \
      "r" (_a5), "r" (_a6)                                                     \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);                               \
    (long int) resultvar;                                                      \
})

#define INTERNAL_SYSCALL(name, err, nr, args...)                               \
  internal_syscall##nr (SYS_ify (name), err, args)

# define INTERNAL_SYSCALL_ERROR_P(val, err) \
  ((unsigned long int) (long int) (val) >= -4095L)

#ifndef __set_errno
# define __set_errno(Val) errno = (Val)
#endif
/* Set error number and return -1.  A target may choose to return the
   internal function, __syscall_error, which sets errno and returns -1.
   We use -1l, instead of -1, so that it can be casted to (void *).  */
#define INLINE_SYSCALL_ERROR_RETURN_VALUE(err)  \
  ({						\
    __set_errno (err);				\
    -1l;					\
  })

# define INTERNAL_SYSCALL_ERRNO(val, err) (-(val))

# define INLINE_SYSCALL(name, nr, args...) \
  ({                                                                           \
    unsigned long int resultvar = INTERNAL_SYSCALL (name, , nr, args);         \
    if (__glibc_unlikely (INTERNAL_SYSCALL_ERROR_P (resultvar, )))             \
    {                                                                          \
      __set_errno (INTERNAL_SYSCALL_ERRNO (resultvar, ));                      \
      resultvar = (unsigned long int) -1;                                      \
    }                                                                          \
    (long int) resultvar; })

/* Wrappers around system calls should normally inline the system call code.
   But sometimes it is not possible or implemented and we use this code.  */
#ifndef INLINE_SYSCALL
#define INLINE_SYSCALL(name, nr, args...) __syscall_##name (args)
#endif

#define __INLINE_SYSCALL2(name, a1, a2)                        \
  INLINE_SYSCALL (name, 2, a1, a2)

#define __INLINE_SYSCALL3(name, a1, a2, a3)                        \
  INLINE_SYSCALL (name, 3, a1, a2, a3)

#define __INLINE_SYSCALL6(name, a1, a2, a3, a4, a5, a6)                        \
  INLINE_SYSCALL (name, 6, a1, a2, a3, a4, a5, a6)

#define __SYSCALL_CONCAT_X(a, b)     a##b
#define __SYSCALL_CONCAT(a, b)       __SYSCALL_CONCAT_X (a, b)

#define __INLINE_SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __INLINE_SYSCALL_NARGS(...) \
  __INLINE_SYSCALL_NARGS_X (__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __INLINE_SYSCALL_DISP(b, ...) \
  __SYSCALL_CONCAT (b, __INLINE_SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

/* Issue a syscall defined by syscall number plus any other argument
   required.  Any error will be handled using arch defined macros and errno
   will be set accordingly.
   It is similar to INLINE_SYSCALL macro, but without the need to pass the
   expected argument number as second parameter.  */
#define INLINE_SYSCALL_CALL(...) \
  __INLINE_SYSCALL_DISP (__INLINE_SYSCALL, __VA_ARGS__)

/* An architecture may override this.  */
#ifndef MMAP_CALL
# define MMAP_CALL(__nr, __addr, __len, __prot, __flags, __fd, __offset) \
    INLINE_SYSCALL_CALL (__nr, __addr, __len, __prot, __flags, __fd, __offset)
#endif

#ifdef __NR_mmap2
# define LH_MMAP_CALL(addr, len, prot, flags, fd, offset) \
           (void *)MMAP_CALL(mmap2, addr, len, prot, flags, fd, \
                                    (off_t) (offset / MMAP2_PAGE_UNIT))
#else
# define LH_MMAP_CALL(addr, len, prot, flags, fd, offset) \
           (void *)MMAP_CALL(mmap, addr, len, prot, flags, fd, offset)
#endif

// ==========================================================
// We stop importing mmap.h from glibc

#include "lower_half_api.h"

#define PAGE_SIZE              0x1000
#define HUGE_PAGE              0x200000

#define ROUND_UP(addr)  \
    (((unsigned long)addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define ROUND_DOWN(addr) ((unsigned long)addr & ~(PAGE_SIZE - 1))

#define ROUND_UP_HUGE(addr) \
    ((unsigned long)(addr + HUGE_PAGE - 1) & ~(HUGE_PAGE - 1))

/*
 * Note that many of these functions/variables have a corresponding
 * function/variable in the upper half.  The correspondences are:
 * FIXME: rename numRegions to numMmapRegions
 * int numRegions -> int *g_numMmaps
 * MmapInfo_t mmaps[MAX_TRACK] -> MmapInfo_t *g_list
 * FIXME: Multiple changes needed; Also rename it to: lh_core_regions_list
 * LhCoreRegions_t lh_regions_list[MAX_LH_REGIONS]
 *    // Found in mmap_internals.h and also in:
 *    //   ../mtcp_split_process.c (used during restart); and in:
 *    //   ../mpi_plugin.c (used during checkpoint to detect lh core regions)
 *    // Apparently, lh_regions_list[] is never used in lower_half dir.
 *    // In ../mtcp_split_process.c, refers to _core_ regions (lh_[rproxy/head)
 *    // lh_regions_list[] is populated by ../mtcp_split_process.c:startProxy()
 * FIXME:  Rename getLhRegionsListFptr to getLhMmappedRegionsListFptr
 * void *getLhRegionsListFptr -> void *getMmappedListFptr
 * FIXME:  Rename getLhRegionsList to getLhMmappedRegionsList
 * FIXME:  Rename LhCoreRegions_T to LhRegions_t
 * LhCoreRegions_t* getLhRegionsList(int **num) -> void getLhMmapList()
 *                               Uses local fnc.: getMmappedList_t fnc;
 *                               Implying: MmapInfo_t* fnc(int **num);
 * FIXME:  Rename getLhRegionsListFptr to getLhMmappedRegionsListFptr
 * LhCoreRegions_t* getLhRegionsList(int **num) ->
 *                                   void *lh_info.getLhRegionsListFptr(**num)
 * void *resetMmappedListFptr -> lh_info.resetMmappedListFptr
 * void resetMmappedList() -> resetMmappedList_t resetMaps // returns void
 * // numCoreRegions is number of regions of lh_proxy/heap:
 * FIXME:  Rename totalRegions to totalCoreRegions
 * libproxy.c: int totalRegions -> int lh_info.numCoreRegions
 * libproxy.c: int num_lh_core_regions -> lh_info.numCoreRegions;
 *                  // int num_lh_core_regions // local var in startProxy()
 *
 * ../restart_plugin:
 * mtcp_split_process.c: int num_lh_core_regions // num regions of lh_proxy/heap
 * mtcp_restart_plugin.c: int total_lh_regions = lh_info->numCoreRegions
 * ../restart_plugin:  LhCoreRegions_t *lh_regions_list[MAX_LH_REGIONS];
 *                               // same as higher up
 * mtcp_restart_plugin.c: getLhRegionsList_t core_fnc ->
 *                                             lh_info->getLhRegionsListFptr;
 */

int getMmapIdx(const void *);
void* getNextAddr(size_t );

// TODO:
//  1. Make the size of list dynamic
#define MAX_TRACK   1000
extern int numRegions;
extern MmapInfo_t mmaps[MAX_TRACK];
extern void *nextFreeAddr;

extern int endOfHeapFrozen;

#endif /* MMAP_INTERNAL_LINUX_H  */
