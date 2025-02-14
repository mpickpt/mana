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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "lower-half-api.h"
#include "dmtcp.h"
#include "mpi_plugin.h"

int initialized = 0;

void initialize_wrappers();
void reset_wrappers();
static void readLhInfoAddr();
extern "C" pid_t dmtcp_get_real_pid();

LowerHalfInfo_t *lh_info;
proxyDlsym_t pdlsym;

void initialize_wrappers() {
  if (!initialized) {
    readLhInfoAddr();
    initialized = 1;
  }
}

void reset_wrappers() {
  initialized = 0;
}

void* mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
  static __typeof__(&mmap) lowerHalfMmapWrapper = (__typeof__(&mmap)) - 1;
  if (!initialized) {
    initialize_wrappers();
  }
  if (lowerHalfMmapWrapper == (__typeof__(&mmap)) - 1) {
    lowerHalfMmapWrapper = (__typeof__(&mmap))lh_info->mmap;
  }
  void *ret;
  if (mana_state == RUNNING) {
    DMTCP_PLUGIN_DISABLE_CKPT();
  }
  ret = lowerHalfMmapWrapper(addr, length, prot, flags, fd, offset);
  if (mana_state == RUNNING) {
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return ret;
}

int munmap(void *addr, size_t length) {
  static __typeof__(&munmap) lowerHalfMunmapWrapper = (__typeof__(&munmap)) - 1;
  if (!initialized) {
    initialize_wrappers();
  }
  if (lowerHalfMunmapWrapper == (__typeof__(&munmap)) - 1) {
    lowerHalfMunmapWrapper = (__typeof__(&munmap))lh_info->munmap;
  }
  int ret;
  if (mana_state == RUNNING) {
    DMTCP_PLUGIN_DISABLE_CKPT();
  }
  ret = lowerHalfMunmapWrapper(addr, length);
  if (mana_state == RUNNING) {
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return ret;
}

static void readLhInfoAddr() {
  char *addr_str = getenv("MANA_LH_INFO_ADDR");
  // If the env var is set, MANA is launching. Otherwise, MANA is restarting
  if (addr_str != NULL) {
    lh_info = (LowerHalfInfo_t*) strtol(addr_str, NULL, 16);
  } else {
    // File name format: mana_tmp_lh_info_[hostname]_[pid]
    char filename[100] = "/tmp/mana_tmp_lh_info_";
    gethostname(filename + strlen(filename), 100 - strlen(filename));
    filename[strlen(filename)] = '_';
    // Convert real pid to char* without calling snprintf
    // During process startup, avoid directly or indirectly
    // calling a libc function that is a DMTCP wrapper.
    int real_pid = dmtcp_get_real_pid();
    static char buf[32] = {0};
    int i = 30;
    for(; real_pid && i ; --i, real_pid /= 10) {
      buf[i] = "0123456789"[real_pid % 10];
    }
    memcpy(filename + strlen(filename), &buf[i+1], strlen(&buf[i+1]));
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
      printf("Could not open %s for reading.\n", filename);
      exit(-1);
    }
    ssize_t rc = read(fd, &lh_info, sizeof(lh_info));
    if (rc != (ssize_t)sizeof(lh_info)) {
      perror("Read fewer bytes than expected from addr.bin.\n");
      exit(-1);
    }
    close(fd);
    if (remove(filename) != 0) {
      fprintf(stderr, "Cannot remove MANA tmp file %s\n", filename);
    }
  }
  pdlsym = (proxyDlsym_t)lh_info->lh_dlsym;
}
