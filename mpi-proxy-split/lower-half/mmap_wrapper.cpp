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
#include <vector>
#include <algorithm>

#include "lower_half_api.h"
#include "patch_trampoline.h"
#include "logging.h"
#include "sbrk_wrapper.h"
#include "switch_context.h"

using namespace std;

#define MMAP_OFF_HIGH_MASK ((-(4096ULL << 1) << (8 * sizeof (off_t) - 1)))
#define MMAP_OFF_LOW_MASK  (4096ULL - 1)
#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)
#define _real_mmap mmap
#define _real_munmap munmap
// #define UBUNTU 1

static std::vector<MmapInfo_t> mmaps;

static void* __mmap_wrapper(void* , size_t , int , int , int , off_t );
static void patchLibc(int , void* , char* );
static void addRegionTommaps(void *, size_t);
static int __munmap_wrapper(void *, size_t);
static void updateMmaps(void *, size_t);

off_t get_symbol_offset(char *pathame, char *symbol);

bool compare (MmapInfo_t &a, MmapInfo_t &b) {
  return (a.addr < b.addr);
}

// Returns a pointer to the array of mmap-ed regions
// Sets num to the number of valid items in the array
std::vector<MmapInfo_t> &get_mmapped_list(int *num) {
  *num = mmaps.size();
  // sort the mmaps list by address
  std::sort(mmaps.begin(), mmaps.end(), compare);
  return mmaps;
}

int checkLibrary(int fd, const char* name,
             char* glibcFullPath, size_t size) {
  char procPath[PATH_MAX] = {0};
  char fullPath[PATH_MAX] = {0};
  snprintf(procPath, sizeof procPath, "/proc/self/fd/%d", fd);
  ssize_t len = readlink(procPath, fullPath, sizeof fullPath);
  if (len < 0) {
    DLOG(ERROR, "Failed to get path for %s. Error: %s\n",
         procPath, strerror(errno));
    return 0;
  }
  DLOG(INFO, "checkLibrary: %s\n", fullPath);
  if (strstr(fullPath, name)) {
    DLOG(INFO, "checkLibrary found\n", fullPath);
    strncpy(glibcFullPath, fullPath, size);
    return 1;
  }
  return 0;
}

void* mmap_wrapper(void *addr, size_t length, int prot,
                  int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  ret = __mmap_wrapper(addr, length, prot, flags, fd, offset);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static void* __mmap_wrapper(void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  if (offset & MMAP_OFF_MASK) {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length, PAGE_SIZE);
  ret = _real_mmap(addr, length, prot, flags, fd, offset);
  if (ret != MAP_FAILED) {
    addRegionTommaps(ret, length);
    // DLOG(NOISE, "LH: mmap (%lu): %p @ 0x%zx\n", mmaps.size(), ret, length);
    if (fd > 0) {
      char glibcFullPath[PATH_MAX] = {0};
      int found = checkLibrary(fd, "libc.so", glibcFullPath, PATH_MAX) ||
                  checkLibrary(fd, "libc-", glibcFullPath, PATH_MAX);
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

int munmap_wrapper(void *addr, size_t length) {
  int ret = -1;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  ret = __munmap_wrapper(addr, length);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static int __munmap_wrapper(void *addr, size_t length) {
  int ret = -1;
  if (addr == 0) {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length, PAGE_SIZE);
  ret = _real_munmap(addr, length);
  if (ret != -1) {
    updateMmaps(addr, length);
    DLOG(4, "LH: munmap (%lu): %p @ 0x%zx\n", mmaps.size(), addr, length);
  }
  return ret;
}

static void patchLibc(int fd, void *base, char *glibc)
{
  assert(base);
  assert(fd > 0);
  DLOG(INFO, "Patching libc (%s) @ %p\n", glibc, base);
  // Save incoming offset
  off_t save_offset = lseek(fd, 0, SEEK_CUR);
  off_t mmap_offset;
  off_t munmap_offset;
  off_t sbrk_offset;
#ifdef UBUNTU
  char buf[256] = "/usr/lib/debug";
  buf[sizeof(buf)-1] = '\0';
  memcpy(buf+strlen(buf), glibc, strlen(glibc));
  /* if (access(buf, F_OK) == 0) {
    // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
    //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
    fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
  } */
  mmap_offset = get_symbol_offset(buf, "mmap"); // elf interpreter debug path
  munmap_offset = get_symbol_offset(buf, "munmap"); // elf interpreter debug path
  sbrk_offset = get_symbol_offset(buf, "sbrk"); // elf interpreter debug path
  close(debug_libc_fd);
#else
  mmap_offset = get_symbol_offset(glibc, "mmap");
  munmap_offset = get_symbol_offset(glibc, "munmap");
  sbrk_offset = get_symbol_offset(glibc, "sbrk");
#endif
  assert(mmap_offset);
  assert(munmap_offset);
  assert(sbrk_offset);
  patch_trampoline((void*)base + mmap_offset, (void*)&mmap_wrapper);
  patch_trampoline((void*)base + munmap_offset, (void*)&munmap_wrapper);
  patch_trampoline((void*)base + sbrk_offset, (void*)&sbrk_wrapper);
  DLOG(INFO, "Patched libc (%s) @ %p: offset(sbrk): %zx; offset(mmap): %zx\n",
       glibc, base, sbrk_offset, mmap_offset);
  // Restore file offset to not upset the caller
  lseek(fd, save_offset, SEEK_SET);
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
  std::sort(mmaps.begin(), mmaps.end(), compare);
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
