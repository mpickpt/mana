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

#include "lower_half_api.h"

int initialized = 0;

void initialize_wrappers();
void reset_wrappers();
static void readLhInfoAddr();
extern "C" pid_t dmtcp_get_real_pid();

LowerHalfInfo_t lh_info = {0};
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

static void readLhInfoAddr() {
  char filename[100] = "./lh_info_";
  gethostname(filename + strlen(filename), 100 - strlen(filename));
  snprintf(filename + strlen(filename), 100 - strlen(filename), "_%d", dmtcp_get_real_pid());
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
  remove(filename);
  pdlsym = (proxyDlsym_t)lh_info.lh_dlsym;
}
