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

// FIXME:  This is a hard-wired constant address.  Do we have code
//         elsewhere that checks if this address conflicts with another
//         memory segment?
#define DEFAULT_UH_BASE_ADDR reinterpret_cast<char*>(0xc0000000) // 3 GB

std::vector<MmapInfo_t> allocated_blocks;
// The arena for memory allocation is from the DEFAULT_UH_BASE_ADDR (3GB)
// with size SIZE_MAX/2 (an arbitrary large address). SIZE_MAX is the largest
// size_t supported by the machine.
// FIXME: Make this starting and end address dynamic and configurable
std::vector<MmapInfo_t> free_blocks = {{DEFAULT_UH_BASE_ADDR, SIZE_MAX/2}};
char *max_allocated_addr = NULL;

static void* __mmap_wrapper(void * , size_t , int , int , int , off_t);
static void patchLibc(int , char * , char *);
static void addRegionTommaps(char *, size_t);
static int __munmap_wrapper(void *, size_t);
static void updateMmaps(char *, size_t);

off_t get_symbol_offset(const char *pathame, const char *symbol);

bool mem_compare (MmapInfo_t &a, MmapInfo_t &b) {
  return (a.addr < b.addr);
}

void print_blocks(vector<MmapInfo_t> &blocks) {
  for (auto b : blocks) {
    printf("block %p ~ %p, size %lx\n", b.addr, b.addr + b.len, b.len);
  }
  fflush(stdout);
}

void merge_overlap_blocks(MmapInfo_t new_block, vector<MmapInfo_t> &blocks) {
  MmapInfo_t merged_block = new_block;
  // Find and merge all overlapping blocks in the allocated list.
  size_t blocks_size = blocks.size();
  for (size_t i = 0; i < blocks_size;) {
    char *it_end_addr = blocks[i].addr + blocks[i].len;
    char *merged_end_addr = merged_block.addr + merged_block.len;
    if (it_end_addr < merged_block.addr) {
      i++;
      continue;
    } else if (blocks[i].addr > merged_end_addr) {
      // If the iterator points to a block whose start address
      // is larger than the end address of the merged block, there's
      // no more blocks that may overlap with the merged_block since
      // the allocated list is sorted.
      break;
    } else {
      char *min_start_addr, *max_end_addr;
      if (blocks[i].addr < merged_block.addr) {
        min_start_addr = blocks[i].addr;
      } else {
        min_start_addr = merged_block.addr;
      }
      if (it_end_addr > merged_end_addr) {
        max_end_addr = blocks[i].addr + blocks[i].len;
      } else {
        max_end_addr = merged_end_addr;
      }
      merged_block.addr = min_start_addr;
      merged_block.len = max_end_addr - min_start_addr;
      blocks.erase(blocks.begin() + i);
      blocks_size--;
    }
  }
  // Insert the new allocated block and sort the list.
  blocks.push_back(merged_block);
  std::sort(blocks.begin(), blocks.end(), mem_compare);
}

void remove_overlap_blocks(MmapInfo_t new_block, vector<MmapInfo_t> &blocks) {
  char *new_block_end_addr = new_block.addr + new_block.len;
  char *old_it_start = NULL, *old_it_end = NULL;
  size_t blocks_size = blocks.size();
  for (size_t i = 0; i < blocks_size;) {
    char *it_end_addr = blocks[i].addr + blocks[i].len;
    if (blocks[i].addr == NULL || (blocks[i].addr == old_it_start && it_end_addr == old_it_end)) {
      printf("loop detected\n");
      fflush(stdout);
      volatile int dummy = 1;
      while (dummy);
    } else {
      old_it_start = blocks[i].addr;
      old_it_end = it_end_addr;
    }
    if (it_end_addr <= new_block.addr) {
      i++;
      continue;
    } else if (blocks[i].addr >= new_block_end_addr) {
      // If the iterator points to a block whose start address
      // is larger than the end address of the new block, there's
      // no more blocks that may overlap with the merged_block since
      // the allocated list is sorted.
      break;
    } else {
      MmapInfo_t updated_block;
      if (blocks[i].addr < new_block.addr) {
        if (it_end_addr > new_block.addr && it_end_addr < new_block_end_addr) {
          // Overlap case 1:
          // [==it==]
          //      [==new_block==]
          updated_block.addr = blocks[i].addr;
          updated_block.len = blocks[i].len - (it_end_addr - new_block.addr);
          blocks.erase(blocks.begin() + i);
          blocks_size--;
          if (updated_block.len > 0) {
            blocks.push_back(updated_block);
          }
        } else {
          // Overlap case 2:
          // [==========it==========]
          //      [==new_block==]
          // New free block in front
          updated_block.addr = blocks[i].addr;
          updated_block.len = blocks[i].len - (it_end_addr - new_block.addr);
          blocks.erase(blocks.begin() + i);
          blocks_size--;
          if (updated_block.len > 0) {
            blocks.push_back(updated_block);
          }
          // New free block at back
          updated_block.addr = new_block_end_addr;
          updated_block.len = it_end_addr - new_block_end_addr;
          if (updated_block.len > 0) {
            blocks.push_back(updated_block);
          }
        }
      } else { 
        if (it_end_addr > new_block_end_addr) {
          // Overlap case 3:
          //           [==it==]
          // [==new_block==]
          updated_block.addr = new_block_end_addr;
          updated_block.len = it_end_addr - new_block_end_addr;
          blocks.erase(blocks.begin() + i);
          blocks_size--;
          if (updated_block.len > 0) {
            blocks.push_back(updated_block);
          }
        } else {
          // Overlap case 4:
          //    [==it==]
          // [==new_block==]
          blocks.erase(blocks.begin() + i);
          blocks_size--;
        }
      }
    }
  }
  std::sort(blocks.begin(), blocks.end(), mem_compare);
}

void *next_free_addr(char *addr, size_t length) {
  // Find the a entry in the free_mem_list that has enough size.
  for (auto it = free_blocks.begin(); it != free_blocks.end(); it++) {
    if (it->len > length) {
      addr = it->addr;
      return addr;
    }
  }
  // If no available block found, return error.
  return NULL;
}

// Returns a pointer to the array of mmap-ed regions
// Sets num to the number of valid items in the array
std::vector<MmapInfo_t> &get_mmapped_list(int *num) {
  *num = allocated_blocks.size();
  return allocated_blocks;
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
    DLOG(NOISE, "checkLibrary found: %s\n", fullPath);
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

// This is a mmap wrapper only for restoring memory after restart.
void* restore_mmap(void *addr, size_t length, int prot,
                   int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  length = ROUND_UP(length, PAGE_SIZE);
  if ((char*)addr + length > max_allocated_addr) {
    max_allocated_addr = (char*)addr + length;
  }
  ret = _real_mmap(addr, length, prot, flags | MAP_FIXED, fd, offset);
  if (ret != MAP_FAILED) {
    char *new_block_end_addr = (char*)addr + length;
    MmapInfo_t new_block = {(char*)addr,
                            (size_t)(new_block_end_addr - (char*)addr)};
    merge_overlap_blocks(new_block, allocated_blocks);
    remove_overlap_blocks(new_block, free_blocks);
  }
  return ret;
}

void* mmap_wrapper(void *addr, size_t length, int prot,
                  int flags, int fd, off_t offset) {
  void *ret = MAP_FAILED;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  ret = __mmap_wrapper(addr, length, prot, flags, fd, offset);
  RETURN_TO_UPPER_HALF();
  return ret;
}

static void* __mmap_wrapper(void *addr, size_t length, int prot,
                            int flags, int fd, off_t offset) {
  static char *libc_base_addr = NULL;
  void *ret = MAP_FAILED;
  length = ROUND_UP(length, PAGE_SIZE);
  if (addr == NULL ||
      flags & MAP_FIXED == 0 ||
      (max_allocated_addr != NULL && (char*)addr > max_allocated_addr)) {
    addr = next_free_addr((char*)addr, length);
    if ((char*)addr + length > max_allocated_addr) {
      max_allocated_addr = (char*)addr + length;
    }
  }
#ifdef MAP_FIXED_NOREPLACE
  flags &= ~MAP_FIXED_NOREPLACE;
#endif
  flags |= MAP_FIXED;
  ret = _real_mmap(addr, length, prot, flags, fd, offset);
  if (ret != MAP_FAILED) {
    DLOG(NOISE, "LH: mmap (%lu): addr %p (%p) @ 0x%zx\n",
         allocated_blocks.size(), ret, addr, length);
    // Add the new block to the allocated_blocks and
    // remove it from the free_blocks.
    char *new_block_end_addr = (char*)addr + length;
    MmapInfo_t new_block = {(char*)addr,
                            (size_t)(new_block_end_addr - (char*)addr)};
    merge_overlap_blocks(new_block, allocated_blocks);
    remove_overlap_blocks(new_block, free_blocks);
    // if the fd is not NULL, check if it's the libc. If it's the libc, patch
    // the mmap and munmap functions to use MANA's wrappers.
    if (fd > 0) {
      char glibcFullPath[PATH_MAX] = {0};
      int found = checkLibrary(fd, "libc.so", glibcFullPath, PATH_MAX) ||
                  checkLibrary(fd, "libc-", glibcFullPath, PATH_MAX);
      if (found) {
        if (libc_base_addr == NULL) {
          libc_base_addr = static_cast<char*>(ret);
        }
        if (prot & PROT_EXEC) {
          int rc = mprotect(ret, length, prot | PROT_WRITE);
          if (rc < 0) {
            DLOG(ERROR,
                 "Failed to add PROT_WRITE perms for memory region at: %p "
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
    DLOG(NOISE, "LH: munmap (%lu): %p @ 0x%zx\n",
         allocated_blocks.size(), addr, length);
    char *new_block_end_addr = (char*)addr + length;
    MmapInfo_t new_block = {(char*)addr,
                            (size_t)(new_block_end_addr - (char*)addr)};
    remove_overlap_blocks(new_block, allocated_blocks);
    merge_overlap_blocks(new_block, free_blocks);
  }
  return ret;
}

static void patchLibc(int fd, char *base, char *glibc)
{
  assert(base != NULL);
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
  patch_trampoline(base + mmap_offset, reinterpret_cast<void*>(&mmap_wrapper));
  patch_trampoline(base + munmap_offset, reinterpret_cast<void*>(&munmap_wrapper));
  // Restore file offset to not upset the caller
  lseek(fd, save_offset, SEEK_SET);
}

// We don't need a wrapper for sbrk() because DMTCP has created a page above here
// to make sbrk(0) fail in libc/application.
