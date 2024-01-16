/****************************************************************************
 *  Copyright (C) 2019-2020 by Twinkle Jain, Rohan garg, and Gene Cooperman *
 *  jain.t@husky.neu.edu, rohgarg@ccs.neu.edu, gene@ccs.neu.edu             *
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
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // For MAP_ANONYMOUS
#endif
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "common.h"
#include "kernel-loader.h"
#include "utils.h"
#include "patch_trampoline.h"
#include "switch_context.h"

using namespace std;

#define MMAP_OFF_HIGH_MASK ((-(4096ULL << 1) << (8 * sizeof (off_t) - 1)))
#define MMAP_OFF_LOW_MASK  (4096ULL - 1)
#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)
#define _real_mmap mmap
#define _real_munmap munmap
// #define UBUNTU 1

static vector<MmapInfo_t> mmaps;

static void* __mmapWrapper(void* , size_t , int , int , int , off_t );
static void patchLibc(int , const void* , const char* );
static void addRegionTommaps(void *, size_t);
static int __munmapWrapper(void *, size_t);
static void updateMmaps(void *, size_t);


bool compare (MmapInfo_t &a, MmapInfo_t &b) {
  return (a.addr < b.addr);
}

// Returns a pointer to the array of mmap-ed regions
// Sets num to the number of valid items in the array
vector<MmapInfo_t> &getMmappedList(int *num) {
  *num = mmaps.size();
  // sort the mmaps list by address
  sort(mmaps.begin(), mmaps.end(), compare);
  return mmaps;
}

void* mmapWrapper(void *addr, size_t length, int prot,
                  int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  JUMP_TO_LOWER_HALF(lhInfo.lhFsAddr);
  ret = __mmapWrapper(addr, length, prot, flags, fd, offset);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static void* __mmapWrapper(void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  if (offset & MMAP_OFF_MASK) {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length);
  ret = _real_mmap(addr, length, prot, flags, fd, offset);
  if (ret != MAP_FAILED) {
    addRegionTommaps(ret, length);
    // DLOG(NOISE, "LH: mmap (%lu): %p @ 0x%zx\n", mmaps.size(), ret, length);
    if (fd > 0) {
      char glibcFullPath[PATH_MAX] = {0};
      int found = checkLibrary(fd, "libc-", glibcFullPath, PATH_MAX);
      if (found && (prot & PROT_EXEC)) {
        int rc = mprotect(ret, length, prot | PROT_WRITE);
        if (rc < 0) {
          DLOG(ERROR, "Failed to add PROT_WRITE perms for memory region at: %p "
               "of: %zu bytes. Error: %s\n", addr, length, strerror(errno));
          return NULL;
        }
        patchLibc(fd, ret, glibcFullPath);
        rc = mprotect(ret, length, prot);
        if (rc < 0) {
          DLOG(ERROR, "Failed to restore perms for memory region at: %p "
               "of: %zu bytes. Error: %s\n", addr, length, strerror(errno));
          return NULL;
        }
      }
    }
  }
  return ret;
}

int munmapWrapper(void *addr, size_t length) {
  int ret = -1;
  JUMP_TO_LOWER_HALF(lhInfo.lhFsAddr);
  ret = __munmapWrapper(addr, length);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static int __munmapWrapper(void *addr, size_t length) {
  int ret = -1;
  if (addr == 0) {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length);
  ret = _real_munmap(addr, length);
  if (ret != -1) {
    updateMmaps(addr, length);
    DLOG(4, "LH: munmap (%lu): %p @ 0x%zx\n", mmaps.size(), addr, length);
  }
  return ret;
}

static void patchLibc(int fd, const void *base, const char *glibc)
{
  assert(base);
  assert(fd > 0);
  const char *MMAP_SYMBOL_NAME = "mmap";
  const char *MUNMAP_SYMBOL_NAME = "munmap";
  const char *SBRK_SYMBOL_NAME = "sbrk";
  DLOG(INFO, "Patching libc (%s) @ %p\n", glibc, base);
  // Save incoming offset
  off_t saveOffset = lseek(fd, 0, SEEK_CUR);
  off_t mmapOffset;
  off_t munmapOffset;
  off_t sbrkOffset;
#ifdef UBUNTU
  char buf[256] = "/usr/lib/debug";
  buf[sizeof(buf)-1] = '\0';
  memcpy(buf+strlen(buf), glibc, strlen(glibc));
  /* if (access(buf, F_OK) == 0) {
    // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
    //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
    fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
  } */
  int debug_libc_fd = open(buf, O_RDONLY);
  assert(debug_libc_fd != -1);
  mmapOffset = get_symbol_offset(debug_libc_fd, buf, MMAP_SYMBOL_NAME);
  munmapOffset = get_symbol_offset(debug_libc_fd, buf, MUNMAP_SYMBOL_NAME);
  sbrkOffset = get_symbol_offset(debug_libc_fd, buf, SBRK_SYMBOL_NAME);
  close(debug_libc_fd);
#else
  mmapOffset = get_symbol_offset(fd, glibc, MMAP_SYMBOL_NAME);
  munmapOffset = get_symbol_offset(fd, glibc, MUNMAP_SYMBOL_NAME);
  sbrkOffset = get_symbol_offset(fd, glibc, SBRK_SYMBOL_NAME);
#endif
  assert(mmapOffset);
  assert(munmapOffset);
  assert(sbrkOffset);
  insertTrampoline((VA)base + mmapOffset, (void*)&mmapWrapper);
  insertTrampoline((VA)base + munmapOffset, (void*)&munmapWrapper);
  insertTrampoline((VA)base + sbrkOffset, (void *)&sbrkWrapper);
  DLOG(INFO, "Patched libc (%s) @ %p: offset(sbrk): %zx; offset(mmap): %zx\n",
       glibc, base, sbrkOffset, mmapOffset);
  // Restore file offset to not upset the caller
  lseek(fd, saveOffset, SEEK_SET);
}

void addRegionTommaps(void * addr, size_t length) {
  MmapInfo_t newRegion;
  newRegion.addr = addr;
  newRegion.len = length;
  mmaps.push_back(newRegion);
}


void updateMmaps(void *addr, size_t length) {
  // traverse through the mmap'ed list and check whether to remove the whole
  // entry or update the address and length
  uint64_t unmaped_start_addr = (uint64_t)addr;
  uint64_t unmaped_end_addr = (uint64_t)addr + length;
  // sort the mmaps list by address
  sort(mmaps.begin(), mmaps.end(), compare);
  // iterate over the list
  for (auto it = mmaps.begin(); it != mmaps.end(); it++) {
    uint64_t start_addr = (uint64_t)it->addr;
    uint64_t end_addr = (uint64_t)(start_addr + it->len);
    // if unmaped start address is same as mmaped start address
    if (start_addr == unmaped_start_addr) {
      if (unmaped_end_addr ==  end_addr) {
        // remove full entry
        mmaps.erase(it);
        return;
      } else if (end_addr > unmaped_end_addr) {
          it->addr = (void *)unmaped_end_addr;
          it->len = it->len - length;
          return;
        } else {
        // if the unmaped region is going beyond the len
        unmaped_start_addr = end_addr;
        length -= it->len;
        mmaps.erase(it);
        it--;
      }
    } else if ((unmaped_start_addr < start_addr) && (unmaped_end_addr > start_addr)) {
      if (unmaped_end_addr ==  end_addr) {
        mmaps.erase(it);
        return;
      } else if (end_addr > unmaped_end_addr) {
          it->addr = (void *)unmaped_end_addr;
          it->len = end_addr - unmaped_end_addr;
          return;
        } else {
        // if the unmaped region is going beyond the len
        length -= length - (end_addr - unmaped_start_addr);
        unmaped_start_addr = end_addr;
        mmaps.erase(it);
        it--;
      }
    } else if ((unmaped_start_addr > start_addr) && (unmaped_start_addr <= end_addr)) {
        it->len = unmaped_start_addr - start_addr;
        if (unmaped_end_addr ==  end_addr) {
          return;
        } else if (end_addr > unmaped_end_addr) {
          MmapInfo_t new_entry;
          new_entry.addr = (void *)unmaped_end_addr;
          new_entry.len = end_addr - unmaped_end_addr;
          mmaps.push_back(new_entry);
          return;
        } else {
          // if the unmaped region is going beyond the len
          length = length - (end_addr - unmaped_start_addr);
          unmaped_start_addr = end_addr;
        }
    }
  }
}
