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

#ifndef SHM_INTERNAL_H
# define SHM_INTERNAL_H 1

#include "lower_half_api.h"

#ifndef __set_errno
# define __set_errno(Val) errno = (Val)
#endif

typedef struct __ShmInfo
{
  int shmid;
  size_t size;
} ShmInfo_t;

extern void* __real_shmat(int shmid, const void *shmaddr, int shmflg);
extern int __real_shmget(key_t key, size_t size, int shmflg);

extern int getShmIdx(int shmid);
extern void addShm(int shmid, size_t size);

// FIXME: Make this dynamic
#define MAX_SHM_TRACK 20
extern int shmidx;
extern ShmInfo_t shms[MAX_SHM_TRACK];

#endif // ifndef SHM_INTERNAL_H
