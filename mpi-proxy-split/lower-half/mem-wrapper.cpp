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

#include "lower-half-api.h"
#include "patch-trampoline.h"
#include "logging.h"
#include "mem-wrapper.h"
#include "switch-context.h"

using namespace std;

#define MMAP_OFF_HIGH_MASK ((-(4096ULL << 1) << (8 * sizeof (off_t) - 1)))
#define MMAP_OFF_LOW_MASK  (4096ULL - 1)
#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)
#define _real_mmap mmap
#define _real_munmap munmap

#define DEFAULT_UH_BASE_ADDR ((void *)0xC0000000) // 3GB

static std::vector<MmapInfo_t> mmaps;

static void* __mmap_wrapper(void* , size_t , int , int , int , off_t );
static void patchLibc(int , void* , char* );
static void addRegionTommaps(void *, size_t);
static int __munmap_wrapper(void *, size_t);
static void updateMmaps(void *, size_t);

void *curr_uh_free_addr = NULL;

off_t get_symbol_offset(char *pathame, char *symbol);

void *get_next_addr(size_t len) {
  void *addr = curr_uh_free_addr;
  curr_uh_free_addr += ROUND_DOWN(len, PAGE_SIZE) + PAGE_SIZE;
  return addr;
}

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
  DLOG(NOISE, "checkLibrary: %s\n", fullPath);
  if (strstr(fullPath, name)) {
    DLOG(NOISE, "checkLibrary found\n", fullPath);
    strncpy(glibcFullPath, fullPath, size);
    return 1;
  }
  return 0;
}


void block_if_contains(void *target, void *addr, size_t length) {
  if (addr <= (char*) target && (char*) addr + length >= (char*) target) {
    printf("%p detected at mmap. Paused.\n", target);
    fflush(stdout);
    volatile int dummy = 1;
    while (dummy);
  }
}

void* mmap_wrapper(void *addr, size_t length, int prot,
                  int flags, int fd, off_t offset) {
  if (curr_uh_free_addr == NULL) {
    char *user_uh_bass_addr = getenv("MANA_UH_BASS_ADDR");
    if (user_uh_bass_addr == NULL) {
      curr_uh_free_addr = DEFAULT_UH_BASE_ADDR;
    } else {
      curr_uh_free_addr = (void*)strtoll(user_uh_bass_addr, NULL, 0);
    }
  }
  void *ret = MAP_FAILED;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  ret = __mmap_wrapper(addr, length, prot, flags, fd, offset);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static void* __mmap_wrapper(void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset) {
  static void *libc_base_addr = NULL;
  void *ret = MAP_FAILED;
  if (flags & MAP_FIXED) {
    if (addr > curr_uh_free_addr) {
      get_next_addr(length);
    }
    DLOG(INFO, "User calls mmap with MAP_FIXED\n");
#ifdef MAP_FIXED_NOREPLACE
  } else if (flags & MAP_FIXED_NOREPLACE) {
    if (addr > curr_uh_free_addr) {
      get_next_addr(length);
    }
    DLOG(INFO, "User calls mmap with MAP_FIXED_NOREPLACE\n");
#endif
  } else {
    addr = get_next_addr(length);
  }
#ifdef MAP_FIXED_NOREPLACE
  flags &= ~MAP_FIXED_NOREPLACE;
#endif
  flags |= MAP_FIXED;
  if (offset & MMAP_OFF_MASK) {
    errno = EINVAL;
    return ret;
  }
  length = ROUND_UP(length, PAGE_SIZE);
  ret = _real_mmap(addr, length, prot, flags, fd, offset);
  if (ret != MAP_FAILED) {
    addRegionTommaps(ret, length);
    DLOG(NOISE, "LH: mmap (%lu): addr %p (%p) @ 0x%zx\n", mmaps.size(), ret, addr, length);
    if (fd > 0) {
      char glibcFullPath[PATH_MAX] = {0};
      int found = checkLibrary(fd, "libc.so", glibcFullPath, PATH_MAX) ||
                  checkLibrary(fd, "libc-", glibcFullPath, PATH_MAX);
      if (found) {
        if (libc_base_addr == NULL) {
          libc_base_addr = ret;
        }
        if (prot & PROT_EXEC) {
          int rc = mprotect(ret, length, prot | PROT_WRITE);
          if (rc < 0) {
            DLOG(ERROR, "Failed to add PROT_WRITE perms for memory region at: %p "
                "of: %zu bytes. Error: %s\n", addr, length, strerror(errno));
            return NULL;
          }
          patchLibc(fd, libc_base_addr, glibcFullPath);
          rc = mprotect(ret, length, prot);
          if (rc < 0) {
            DLOG(ERROR, "Failed to restore perms for memory region at: %p "
                "of: %zu bytes. Error: %s\n", addr, length, strerror(errno));
            return NULL;
          }
          libc_base_addr = NULL;
        }
      }
    }
  }
  return ret;
}

int munmap_wrapper(void *addr, size_t length) {
  int ret = -1;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
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
    DLOG(NOISE, "LH: munmap (%lu): %p @ 0x%zx\n", mmaps.size(), addr, length);
    updateMmaps(addr, length);
  }
  return ret;
}

static void patchLibc(int fd, void *base, char *glibc)
{
  assert(base);
  assert(fd > 0);
  DLOG(NOISE, "Patching libc (%s) @ %p\n", glibc, base);
  // Save incoming offset
  off_t save_offset = lseek(fd, 0, SEEK_CUR);
  off_t mmap_offset = get_symbol_offset(glibc, "mmap");
  if (! mmap_offset) {
    char buf[256] = "/usr/lib/debug";
    buf[sizeof(buf)-1] = '\0';
    ssize_t rc = 0;
    rc = readlink(glibc, buf+strlen(buf), sizeof(buf)-strlen(buf)-1);
    if (rc != -1 && access(buf, F_OK) == 0) {
      // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
    }
    mmap_offset = get_symbol_offset(buf, "mmap"); // elf interpreter debug path
  }
  assert(mmap_offset);
  off_t munmap_offset = get_symbol_offset(glibc, "munmap");
  if (! munmap_offset) {
    char buf[256] = "/usr/lib/debug";
    buf[sizeof(buf)-1] = '\0';
    ssize_t rc = 0;
    rc = readlink(glibc, buf+strlen(buf), sizeof(buf)-strlen(buf)-1);
    if (rc != -1 && access(buf, F_OK) == 0) {
      // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
    }
    munmap_offset = get_symbol_offset(buf, "munmap"); // elf interpreter debug path
  }
  assert(munmap_offset);
  patch_trampoline((void*)base + mmap_offset, (void*)&mmap_wrapper);
  patch_trampoline((void*)base + munmap_offset, (void*)&munmap_wrapper);
  // Restore file offset to not upset the caller
  lseek(fd, save_offset, SEEK_SET);
}

void addRegionTommaps(void * addr, size_t length) {
  MmapInfo_t newRegion;
  newRegion.addr = (char*)addr;
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
          it->addr = (char *)unmaped_end_addr;
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
          it->addr = (char *)unmaped_end_addr;
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
          new_entry.addr = (char *)unmaped_end_addr;
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

// We don't need a wrapper for sbrk() because DMTCP has created a page above here to make sbrk(0) fail in libc/application.
