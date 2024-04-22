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

#ifndef MMAP_WRAPPER_H
#define MMAP_WRAPPER_H
#include <vector>
#include "lower_half_api.h"
void* mmap_wrapper(void *, size_t , int , int , int , off_t );
int munmap_wrapper(void *, size_t);
void set_end_of_heap(void *);
std::vector<MmapInfo_t> &get_mmapped_list(int *num);
void set_end_of_heap(void *addr);
void set_uh_brk(void *addr);
void* sbrk_wrapper(intptr_t increment);
void * get_end_of_heap();
extern void *__startOfReservedHeap;
extern void *__endOfReservedHeap;
#endif // MMAP_WRAPPER_H
