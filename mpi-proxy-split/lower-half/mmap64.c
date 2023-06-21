/* mmap - map files or devices into memory.  Linux version.
   Copyright (C) 1999-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Jakub Jelinek <jakub@redhat.com>, 1999.

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

#include <linux/version.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
// #include <sysdep.h>
// #include <mmap_internal.h>
#include "mmap_internal.h"
#include "mpi_copybits.h"
#include <sys/syscall.h>

#define HAS_MAP_FIXED_NOREPLACE LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

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

/* To avoid silent truncation of offset when using mmap2, do not accept
   offset larger than 1 << (page_shift + off_t bits).  For archictures with
   32 bits off_t and page size of 4096 it would be 1^44.  */
#define MMAP_OFF_HIGH_MASK \
  ((-(MMAP2_PAGE_UNIT << 1) << (8 * sizeof (off_t) - 1)))

#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)

/* An architecture may override this.  */
#ifndef MMAP_PREPARE
# define MMAP_PREPARE(addr, len, prot, flags, fd, offset)
#endif

// Source code copied from glibc-2.27/sysdeps/unix/sysv/linux/mmap64.c
// Modified to keep track of regions mmaped by lower half

// TODO:
//  1. Add byte, 0b10101010, everywhere to lh_memRange, when first
//     calling mmap within 'if (!initialized)' logic, below.
//  2. Make the size of list dynamic

// Number of valid objects in the 'mmaps' list below
int numRegions = 0;

#ifdef LIBMMAP_SO
// Lower-half memory range to use.
MemRange_t lh_memRange = {0};
#endif

// List of regions mmapped by the lower half
MmapInfo_t mmaps[MAX_TRACK] = {{0}};

// Pointer to the next free address used for allocation
void *nextFreeAddr = NULL;

// Returns a pointer to the array of mmap-ed regions
// Sets num to the number of valid items in the array
MmapInfo_t*
getMmappedList(int *num)
{
  if (!num) return NULL;
  *num = numRegions;
  return mmaps;
}

void
resetMmappedList()
{
  for (int i = 0; i < numRegions; i++) {
    memset(&mmaps[i], 0, sizeof(mmaps[i]));
  }
  numRegions = 0;
}

int
getMmapIdx(const void *addr)
{
  for (int i = 0; i < numRegions; i++) {
    if (mmaps[i].addr == addr) {
      return i;
    }
  }
  return -1;
}

// Handle non-huge pages; returns page aligned address (4 KB alignment) from
// within the lower half memory range
void*
getNextAddr(size_t len)
{
  if (lh_memRange.start == NULL && nextFreeAddr == NULL) {
    return NULL;
  } else if (lh_memRange.start != NULL && nextFreeAddr == NULL) {
    nextFreeAddr = lh_memRange.start;
  }

  // Get a 4 KB aligned region; nextFreeAddr is always 4 KB aligned
  void *curr = nextFreeAddr;

  // Move the pointer to the next free address
  nextFreeAddr = (char*)curr + ROUND_UP(len) + PAGE_SIZE;

  // Assert if we have gone past the end of the lower half memory range
  assert(nextFreeAddr <= lh_memRange.end);
  return curr;
}

static int
extendExistingMmap(const void *addr)
{
  for (int i = 0; i < numRegions; i++) {
    if (addr >= mmaps[i].addr && addr <= mmaps[i].addr + mmaps[i].len) {
      return i;
    }
  }
  return -1;
}

// Handle huge pages; returns huge page aligned address (2 MB alignment) from
// within the lower half memory range
static void*
getNextHugeAddr(size_t len)
{
  if (lh_memRange.start == NULL && nextFreeAddr == NULL) {
    return NULL;
  } else if (lh_memRange.start != NULL && nextFreeAddr == NULL) {
    nextFreeAddr = lh_memRange.start;
  }

  // Get a 2 MB aligned region
  void *curr = (void*)ROUND_UP_HUGE((unsigned long)nextFreeAddr);

  // Move the pointer to the next free address
  nextFreeAddr = (char*)curr + ROUND_UP(len) + PAGE_SIZE;

  // Assert if we have gone past the end of the lower half memory range
  if (nextFreeAddr > lh_memRange.end) {
    assert(0);
  }
  return curr;
}

void *
__mmap64 (void *addr, size_t len, int prot, int flags, int fd, __off_t offset)
{
  static int initialized = 0;
  if (!initialized) {
#ifdef LIBMMAP_SO
    int length = 100*1024*1024; // 100 MB
    lh_memRange.start = mmap(NULL, length,
                             PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    lh_memRange.end = lh_memRange.start + length;
#else
    int length = lh_memRange.end - lh_memRange.start;
# if 0
    // FIXME: After we copy into upper half, call mmap as below.
    //        But don't call this when lh_proxy first executes by itself.
    //        So, we need to recognize if we are executing lh_proxy
    //          or the target MPI application.
#  if HAS_MAP_FIXED_NOREPLACE
    void *rc = mmap(lh_memRange.start, length,
                    PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,
                    -1, 0);
    if (rc == MAP_FAILED && errno == EEXIST) {
      char msg[] = "*** Panic: MANA lower half: can't initialize lh_memRange\n";
      write(2, msg, sizeof(msg)); assert(rc == 0);
    }
#  else
    void *rc = mmap(lh_memRange.start, length,
                    PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (rc != lh_memRange.start) {
      if (rc != MAP_FAILED) {
        munmap(rc, length);
      }
      char msg[] = "*** Panic: MANA lower half: can't initialize lh_memRange\n";
      write(2, msg, sizeof(msg)); assert(rc != lh_memRange.start);
    }
#  endif
# endif
#endif
    initialized = 1;
  }

  void *ret = MAP_FAILED;
  MMAP_CHECK_PAGE_UNIT ();

  if (offset & MMAP_OFF_MASK)
    return (void *) INLINE_SYSCALL_ERROR_RETURN_VALUE (EINVAL);

  MMAP_PREPARE (addr, len, prot, flags, fd, offset);

  int extraGuardPage = 0;
  size_t totalLen = len;

  if (addr == NULL) {
    // FIXME: This should be made more generic; perhaps use MAP_HUGETLB?
    //        Perhaps a simpler solution is to "module unload hugetlbfs"
    // FIXME: Handle the case of the caller calling us with MAP_FIXED
    //        This code cannot handle this scenario
    if (len != 0x400000 &&
        len != 0x600000 &&
        len != 0x800000 &&
        len != 0xC00000 &&
        len != 0xE00000 &&
        len != 0x1400000 &&
        len != 0xA00000 &&
        len != 0x1600000) {
      addr = getNextAddr(len);
    } else {
      if (fd > 0) {
        // TODO: Check fd is pointing to hugetlbfs
        addr = getNextHugeAddr(len);
      } else {
        // FIXME:  This appears to be for hugetlbfs with no backing store.
        //         How is it used?
        addr = NULL;
      }
    }
    // FIXME: Check if original caller had used MAP_FIXED
    //          or MAP_FIXED_NOREPLACE; If so, we have a big problem here.
    //          Abort and tell user for now??  Or just make the
    //          call directly now, and hope for the best??
    if (addr) {
#if HAS_MAP_FIXED_NOREPLACE
      flags |= MAP_FIXED_NOREPLACE;
#else
      flags |= MAP_FIXED; // Dangerous!  We might clobber unknown mmap region.
#endif
    }
  }

  // FIXME: This is a temporary hack. Can we remove this magic number
  // and make this test more robust. It seems like a test for MAP_FIXED
  // would not work.
  if (len > 0x400000 && addr == NULL) {
    extraGuardPage = 1;
    totalLen += 2 * PAGE_SIZE;
  }
  while (1) {
#ifdef __NR_mmap2
    if (addr) {
      int rc = munmap(addr, totalLen);
      if (rc == -1) {
        char msg[] = "*** Panic: MANA lower half: can't munmap a region.\n";
        write(2, msg, sizeof(msg)); assert(rc == 0);
      }
    }
# define MMAP mmap2
    ret = (void *) MMAP_CALL (MMAP, addr, totalLen, prot, flags, fd,
                                    (off_t) (offset / MMAP2_PAGE_UNIT));
#else
# define MMAP mmap
    ret = (void *) MMAP_CALL (MMAP, addr, totalLen, prot, flags, fd, offset);
#endif

    if (ret == MAP_FAILED && errno == EEXIST) {
      // Someone stole our mmap slot (via ioctl?).  This can happen if
      //   a network or shared-memory library (e.g. xpmem) uses ioctl
      //   to allocate memory.  Look for the next available address.
      do { addr += PAGE_SIZE;
         }
      while ((void *)MMAP_CALL(MMAP, addr, PAGE_SIZE, PROT_NONE, MAP_PRIVATE,
                               0, -1)
             == MAP_FAILED);
      nextFreeAddr = addr;
      // FIXME:  We need to recognize dontuse everywhere.
      mmaps[getMmapIdx(ret)].dontuse = 1; // Skip past the stolen region.
      mmaps[getMmapIdx(ret)].len = addr-ret; // Set length of stolen region.
      addr = getNextAddr(len); // FIXME: Maybe we want getNextHugeAddr()
    } else {
      break;
    }
  }  // end of while loop using MMAP_CALL

  // Accounting of the lower-half mmap regions
  // XXX (Rohan): Why are we doing this? Given that all the lower half memory
  //      allocations are restricted to a specified range, do we need to do
  //      this?
  if (ret != MAP_FAILED) {
    int idx = getMmapIdx(ret);
    if (idx != -1) {
      mmaps[idx].len = ROUND_UP(len);
      mmaps[idx].unmapped = 0;
      mmaps[idx].dontuse = 0;
    } else {
      int idx2 = extendExistingMmap(ret);
      if (idx2 != -1) {
        size_t length = ROUND_UP(len) + ((char*)ret - (char*)mmaps[idx2].addr);
        mmaps[idx2].len = length > mmaps[idx2].len ? length : mmaps[idx2].len;
        mmaps[idx2].unmapped = 0;
        idx = idx2;
      } else {
        mmaps[numRegions].addr = ret;
        mmaps[numRegions].len = ROUND_UP(len);
        mmaps[numRegions].unmapped = 0;
        idx = numRegions;
        numRegions = (numRegions + 1) % MAX_TRACK;
        // FIXME: A better fix, below, is to set nextFreeAddr back to the start,
        //   and then to test the size of any gap.  That can be done
        //   by checking each known region.  Hopefully, there are not many.
        //   Or at least, we can re-use any unmapped regions.
        if (numRegions == 0) {
          char msg[] = "*** Panic: MANA lower half: no more space for mmap.\n";
          write(2, msg, sizeof(msg)); assert(numRegions > 0);
        }
      }
    }
    if (extraGuardPage) {
      mmaps[idx].guard = 1;
      void *lastPage = (char*)ret + ROUND_UP(totalLen) - PAGE_SIZE;
      void *firstPage = ret;
      mprotect(firstPage, PAGE_SIZE, PROT_NONE);
      mprotect(lastPage, PAGE_SIZE, PROT_NONE);
      mmaps[idx].addr = (char*)ret + PAGE_SIZE;
      ret = mmaps[idx].addr;
    }
  }
  return ret;
}
// weak_alias (__mmap64, mmap64)
// libc_hidden_def (__mmap64)

#ifdef LIBMMAP_SO
extern __typeof (__mmap64) mmap64 __attribute__ ((alias ("__mmap64")));
#else
extern __typeof (__mmap64) __mmap64 __attribute__ ((visibility ("hidden")));
extern __typeof (__mmap64) mmap64 __attribute__ ((weak, alias ("__mmap64")));
#endif

#ifdef __OFF_T_MATCHES_OFF64_T
// weak_alias (__mmap64, mmap)
// weak_alias (__mmap64, __mmap)
// libc_hidden_def (__mmap)
# ifdef LIBMMAP_SO
extern __typeof (__mmap64) mmap __attribute__ ((alias ("__mmap64")));
extern __typeof (__mmap64) __mmap __attribute__ ((alias ("__mmap64")));
# else
extern __typeof (__mmap64) mmap __attribute__ ((weak, alias ("__mmap64")));
extern __typeof (__mmap64) __mmap __attribute__ ((weak, alias ("__mmap64")));
extern __typeof (__mmap) __mmap __attribute__ ((visibility ("hidden")));
# endif
#endif
