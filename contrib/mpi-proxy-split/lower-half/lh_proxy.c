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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ucontext.h>

#include "mpi_copybits.h"

int main(int argc, char **argv, char **envp)
{
  if (argc >= 1) { // run standalone, if no pipefd
    // lh_info is defined in split-process.cpp
    DLOG(INFO, "startText: %p, endText: %p, endOfHeap: %p\n",
         lh_info.startText, lh_info.endText, lh_info.endOfHeap);
    // We're done initializing; jump back to the upper half
    // g_appContext would have been set by the upper half
    int ret = setcontext(lh_info.g_appContext);
    if (ret < 0) {
      DLOG(ERROR, "setcontext failed: %s", strerror(errno));
    }
    return 0;
  }

  return 0;
}
