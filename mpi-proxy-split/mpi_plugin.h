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

#include "lower_half_api.h"
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

extern int g_numMmaps;
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

/******************************************************************************/
/* 
In applications using MPI, an MPI thread is responsible for managing asynchronous requests in the background. During checkpointing, it is necessary to suspend Collective and P2P communication so that MANA can drain messages from the network and create a checkpoint image. Previously, we had been draining collective messages during the PRESUSPEND event, while all threads were still running. This is because draining collective messages requires all ranks to be in a consistent state, which can only be achieved through trial and error and by allowing the user application to make progress. However, we were draining P2P messages after suspending all threads, including the MPI thread. This was because each rank explicitly asks other ranks for any pending messages floating in the network, and MANA drains them into its internal buffer so that they can be passed to the user application during checkpoint restart. Additionally, we rely on MPI to provide information about pending requests or messages in the network.

While this approach seemed reasonable, it did not account for a case where the MPI thread was creating metadata for a P2P request and was suspended in between. In such a scenario, that message would not be visible to other ranks since the MPI APIs used by MANA to gather information about pending requests would report that there are no messages, even though there is a message pending that is not yet visible to all ranks because its metadata has not yet been generated.
*/

/*
The P2P communication in an application is controlled globally by <global_p2p_communication> variable. When we say "control", we mean that it suspends P2P communication and prevents the user application from being in the lower half. This allows for P2P message draining and checkpointing during the PRESUSPEND checkpoint event. If the P2P message draining is performed after PRESUSPEND and before PRECHECKPOINT, the variable mentioned above is not needed.
*/
static int global_p2p_communication = 1;
static int internal_p2p_communication = 0;

void global_p2p_communication_barrier();

/******************************************************************************/

#endif // ifndef _MPI_PLUGIN_H
