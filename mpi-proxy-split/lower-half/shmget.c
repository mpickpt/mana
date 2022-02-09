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

#include <sys/shm.h>
#include <errno.h>

#include "shm_internal.h"

int shmidx = 0;
ShmInfo_t shms[MAX_SHM_TRACK] = {0};

int
getShmIdx(int shmid)
{
  for (int i = 0; i < MAX_SHM_TRACK; i++) {
    if (shms[i].shmid == shmid) {
      return i;
    }
  }
  return -1;
}

void
addShm(int shmid, size_t size)
{
  shms[shmidx].shmid = shmid;
  shms[shmidx].size = size;
  shmidx = (shmidx + 1) % MAX_SHM_TRACK;
}

int
__wrap_shmget(key_t key, size_t size, int shmflg)
{
  int rc = __real_shmget(key, size, shmflg);
  if (rc > 0) {
    addShm(rc, size);
  }
  return rc;
}
