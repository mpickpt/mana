#/*****************************************************************************
# * Copyright (C) 2021 Jun Gan <jun.gan@memverge.com>                         *
# *                                                                           *
# * DMTCP is free software: you can redistribute it and/or                    *
# * modify it under the terms of the GNU Lesser General Public License as     *
# * published by the Free Software Foundation, either version 3 of the        *
# * License, or (at your option) any later version.                           *
# *                                                                           *
# * DMTCP is distributed in the hope that it will be useful,                  *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
# * GNU Lesser General Public License for more details.                       *
# *                                                                           *
# * You should have received a copy of the GNU Lesser General Public          *
# * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
# *****************************************************************************/

#ifndef MEMORY_ACCESS_TRACKER
#define MEMORY_ACCESS_TRACKER

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MemTracker {
  /* TODO: support multiple trackers */
  // struct list_head node;
  void *addr;
  size_t len;
  int fd; // for debug
  uint32_t max_num; // max number of access will be tracked
  /* If there're too many accesses to the same address,
   * May change to use map<addr, count> instead here */
  uint32_t num_access; // number of access happened
  void** addrs; // addresses have been accessed
} MemTracker_t;

struct MemTracker* StartTrackMemory(void *addr, size_t len, uint32_t max_num, int fd);
void EndTrackMemory(struct MemTracker *tracker);
void FreeMemTracker(struct MemTracker *tracker);

#ifdef __cplusplus
}
#endif
#endif
