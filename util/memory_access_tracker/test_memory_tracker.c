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

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "memory_access_tracker.h"

int main ()
{
  int i;
  int log_fd  = open("test.log", O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC | O_TRUNC, 0777);
  if (log_fd < 0) {
    perror("Failed to open test log");
    exit(EXIT_FAILURE);
  }

  // allocate 1 page memory
  void *test_addr = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (test_addr == MAP_FAILED) {
    close(log_fd);
    perror("Map failed");
    exit(EXIT_FAILURE);
  }

  memset(test_addr, 0, 1024);
  memset(test_addr + 1024, 1, 1024);
  memset(test_addr + 2048, 2, 1024);
  memset(test_addr + 3072, 3, 1024);

  struct MemTracker *tracker = StartTrackMemory(test_addr, 4096, 100, log_fd);

  printf("%s %d Test %p\n", __func__, __LINE__, test_addr);

  // should trigger segv
  uint8_t c = *(uint8_t *)test_addr;
  
  printf("%s %d Read %p: %u\n", __func__, __LINE__, test_addr, c);
  
  *(uint8_t *)test_addr = 1;

  c = *(uint8_t *)test_addr;

  printf("%s %d Read %p: %u\n", __func__, __LINE__, test_addr, c);

  c = *(uint8_t *)(test_addr + 100);
  printf("%s %d Read %p: %u\n", __func__, __LINE__, test_addr + 1024, c);

  memset(test_addr + 2048, 3, 1024);
  EndTrackMemory(tracker);
  printf("%s %d access %u times\n", __func__, __LINE__, tracker->num_access);
  for (i = 0; i < tracker->num_access; i++) {
    printf("%p\n", tracker->addrs[i]);
  }

  FreeMemTracker(tracker);

  close(log_fd);
  return 0;
}
