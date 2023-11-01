/* Copyright (C) 1991-2015 Free Software Foundation, Inc.
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

#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "mmap_internal.h"

/* Defined in brk.c.  */
extern void *__curbrk;
extern int __brk (void *addr);

/* Defined in init-first.c.  */
// extern int __libc_multiple_libcs attribute_hidden;
static int __libc_multiple_libcs = 0;

void *__endOfHeap = 0;

#define PAGE_SIZE 4096

#define ROUND_UP(addr) ((unsigned long)(addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#ifndef __set_errno
# define __set_errno(Val) errno = (Val)
#endif

#ifdef __NR_mmap2
# define LH_MMAP_CALL(addr, len, prot, flags, fd, offset) \
           (void *)MMAP_CALL(mmap2, addr, len, prot, flags, fd, \
                                    (off_t) (offset / MMAP2_PAGE_UNIT))
#else
# define LH_MMAP_CALL(addr, len, prot, flags, fd, offset) \
           (void *)MMAP_CALL(mmap, addr, len, prot, flags, fd, offset)
#endif

/* Extend the process's data space by INCREMENT.
   If INCREMENT is negative, shrink data space by - INCREMENT.
   Return start of new space allocated, or -1 for errors.  */
void *
__sbrk (intptr_t increment)
{
  void *oldbrk;

  /* If this is not part of the dynamic library or the library is used
     via dynamic loading in a statically linked program update
     __curbrk from the kernel's brk value.  That way two separate
     instances of __brk and __sbrk can share the heap, returning
     interleaved pieces of it.  */
  if (__curbrk == NULL || __libc_multiple_libcs)
    if (__brk (0) < 0)		/* Initialize the break.  */
      return (void *) -1;
    else
      __endOfHeap = __curbrk;

  if (increment == 0)
    return __curbrk;

  oldbrk = __curbrk;
  if (increment > 0
      ? ((uintptr_t) oldbrk + (uintptr_t) increment < (uintptr_t) oldbrk)
      : ((uintptr_t) oldbrk < (uintptr_t) -increment))
    {
      errno = ENOMEM;
      return (void *) -1;
    }

  // if (__brk (oldbrk + increment) < 0)
  //   return (void *) -1;

  if (oldbrk + increment > __endOfHeap) {
    if (LH_MMAP_CALL(__endOfHeap, ROUND_UP(oldbrk + increment - __endOfHeap),
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
             -1, 0) < 0) {
       return (void *) -1;
    }
    if (lh_info.lh_regions_list != NULL) {
      lh_info.lh_regions_list[lh_info.numCoreRegions].start_addr = __endOfHeap;
      lh_info.lh_regions_list[lh_info.numCoreRegions].end_addr = ROUND_UP(oldbrk + increment);
      lh_info.lh_regions_list[lh_info.numCoreRegions].prot = PROT_READ | PROT_WRITE;
      lh_info.numCoreRegions++;
    }
  }

  __endOfHeap = (void*)ROUND_UP(oldbrk + increment);
  __curbrk = oldbrk + increment;

  return oldbrk;
}
// libc_hidden_def (__sbrk)
// weak_alias (__sbrk, sbrk)
extern __typeof (__sbrk) __sbrk __attribute__ ((visibility ("hidden")));
extern __typeof (__sbrk) sbrk __attribute__ ((weak, alias ("__sbrk")));
