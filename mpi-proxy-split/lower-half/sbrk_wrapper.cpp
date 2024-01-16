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
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "common.h"
#include "logging.h"
#include "kernel-loader.h"
#include "getmmap.h"
#include "switch_context.h"

static void *__curbrk;
static void *__endOfHeap = 0;

static void* __sbrkWrapper(intptr_t );

void * getEndOfHeap() {
  return __endOfHeap;
}

void setEndOfHeap(void *addr) {
  __endOfHeap = (void*)ROUND_UP(addr);
}

void setUhBrk(void *addr) {
  __curbrk = addr;
}

void* sbrkWrapper(intptr_t increment) {
  void *addr = NULL;
  JUMP_TO_LOWER_HALF(lhInfo.lhFsAddr);
  addr = __sbrkWrapper(increment);
  RETURN_TO_UPPER_HALF();
  return addr;
}

/* Extend the process's data space by INCREMENT.
   If INCREMENT is negative, shrink data space by - INCREMENT.
   Return start of new space allocated, or -1 for errors.  */
static void* __sbrkWrapper(intptr_t increment) {
  void *oldbrk;

  DLOG(NOISE, "LH: sbrk called with 0x%lx\n", increment);

  if (__curbrk == NULL) {
    if (brk (0) < 0) {
      return (void *) -1;
    } else {
      __endOfHeap = __curbrk;
    }
  }

  if (increment == 0) {
    DLOG(NOISE, "LH: sbrk returning %p\n", __curbrk);
    return __curbrk;
  }

  oldbrk = __curbrk;
  if (increment > 0
      ? ((uintptr_t) oldbrk + (uintptr_t) increment < (uintptr_t) oldbrk)
      : ((uintptr_t) oldbrk < (uintptr_t) -increment))
    {
      errno = ENOMEM;
      return (void *) -1;
    }

  if ((VA)oldbrk + increment > (VA)__endOfHeap) {
    if (mmapWrapper(__endOfHeap,
                    ROUND_UP((VA)oldbrk + increment - (VA)__endOfHeap),
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                    -1, 0) == MAP_FAILED) {
       return (void *) -1;
    }
  }

  __endOfHeap = (void*)ROUND_UP((VA)oldbrk + increment);
  __curbrk = (VA)oldbrk + increment;

  DLOG(NOISE, "LH: sbrk returning %p\n", oldbrk);

  return oldbrk;
}
