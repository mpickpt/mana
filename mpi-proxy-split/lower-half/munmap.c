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

#define _GNU_SOURCE // For mremap
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#include "mmap_internal.h"

/* Deallocate any mapping for the region starting at ADDR and extending LEN
   bytes.  Returns 0 if successful, -1 for errors (and sets errno).  */
// Copied from glibc-2.27 source, with modifications to allow
// tracking of munmapped regions
extern int __real___munmap(void *, size_t );

#ifdef LIBMMAP_SO
// Lower-half memory range to use; initialized in mmap64.c
MemRange_t lh_memRange;
#endif

static inline int
alignedWithLastPage(const void *haystackStart,
               const void *haystackEnd,
               const void *needleStart,
               const void *needleEnd)
{
  return needleStart >= haystackStart && needleEnd == haystackEnd;
}

static int
getOverlappingRegion(void *addr, size_t len)
{
  for (int i = 0; i < numRegions; i++) {
    void *rstart = mmaps[i].addr;
    void *rend = (char*)mmaps[i].addr + mmaps[i].len;
    void *ustart = addr;
    void *uend = (char*)addr + len;
    if (alignedWithLastPage(rstart, rend, ustart, uend)) {
      return i;
    }
  }
  return -1;
}

static void
removeMappings(void *addr, size_t len)
{
  int idx = getMmapIdx(addr);
  if (idx != -1) {
    if (mmaps[idx].len == len) {
      // Caller unmapped the entire region
      mmaps[idx].unmapped = 1;
    } else if (mmaps[idx].len > len) {
      // Move the start addr ahead by len bytes
      mmaps[idx].addr = (char*)addr + len;
      mmaps[idx].len -= len;
    } else {
      // case: len > mmaps[idx].len
      // This cannot happen.
      assert(0);
    }
  } else {
    idx = getOverlappingRegion(addr, len);
    if (idx != -1) {
      void *oldAddr = mmaps[idx].addr + mmaps[idx].len;
      mmaps[idx].len -= len;
    }
  }
  // TODO: Handle the case where the removed region is in the middle of
  // of an mmaped region.
}

int
__wrap___munmap (void *addr, size_t len)
{
  int rc = mprotect(addr, len, PROT_NONE);
  if (rc == 0) {
    removeMappings(addr, len);
  }
  return rc;
}

int
munmap(void *addr, size_t len)
{
  if (addr < lh_info.memRange.start || addr >= lh_info.memRange.end) {
    char msg[] ="Lower half called munmap,"
                " and it's not from the reserved memRange.";
    write(2, msg, sizeof(msg));
    return __real___munmap(addr, len);
  }

  return __wrap___munmap(addr, len);
}

// stub_warning (munmap)
// weak_alias (__munmap, munmap)
// extern __typeof (__munmap) munmap __attribute__ ((weak, alias ("__munmap")));
// extern __typeof (__munmap) __munmap __attribute__ ((visibility ("hidden")));
