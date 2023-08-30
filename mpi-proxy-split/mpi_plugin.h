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

#ifndef _MPI_PLUGIN_H
#define _MPI_PLUGIN_H

#include <mpi.h>
#include <cstdint>
#include <cstddef>

#include "libproxy.h"
#include "dmtcp_dlsym.h"

#define   _real_fork      NEXT_FNC_DEFAULT(fork)

#define NOT_IMPLEMENTED(op)                                         \
{                                                                   \
  if (op > MPIProxy_ERROR && op < MPIProxy_Cmd_Shutdown_Proxy)      \
    fprintf(stdout, "[%d:%s] NOT IMPLEMENTED\n", op, __FUNCTION__); \
  else                                                              \
    fprintf(stdout, "[%s] NOT IMPLEMENTED\n", __FUNCTION__);        \
  exit(EXIT_FAILURE);                                               \
}

#define NOT_IMPL(func)                            \
  EXTERNC func                                    \
  {                                               \
    NOT_IMPLEMENTED(MPIProxy_Cmd_Shutdown_Proxy); \
  }

#define PMPI_IMPL(ret, func, ...)                               \
  EXTERNC ret P##func(__VA_ARGS__) __attribute__ ((weak, alias (#func)));

extern int *g_numMmaps;
extern MmapInfo_t *g_list;

bool isUsingCollectiveToP2p();
void recordPreMpiInitMaps();
void recordPostMpiInitMaps();

enum mana_state_t {
  UNKNOWN_STATE,
  RUNNING,
  CKPT_COLLECTIVE,
  CKPT_P2P,
  RESTART_RETORE,
  RESTART_REPLAY
};

extern mana_state_t mana_state;

#endif // ifndef _MPI_PLUGIN_H
