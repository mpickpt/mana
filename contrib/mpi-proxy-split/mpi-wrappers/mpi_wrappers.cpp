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

#include "mpi_plugin.h"  // lh_info declared via lower_half_api.h
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"
#include "p2p_drain_send_recv.h"

#if 0
DEFINE_FNC(int, Init, (int *) argc, (char ***) argv)
DEFINE_FNC(int, Init_thread, (int *) argc, (char ***) argv,
           (int) required, (int *) provided)
#else
static const char collective_p2p_string[] =
   "\n"
   "   ***************************************************************************\n"
   "   *** The environment variable MPI_COLLECTIVE_P2P was set when this MANA was\n"
   "   ***   compiled.  It's for debugging/testing.  MPI collective calls will be\n"
   "   ***   translated to MPI_Send/Recv, and so the application will be _much_\n"
   "   ***   slower. See mpi_wrappers.cpp and mpi_collective_p2p.c for selecting\n"
   "   ***   individual MPI collective calls for translation to MPI_Send/Recv.\n"
   "   ***************************************************************************\n"
   "\n";
USER_DEFINED_WRAPPER(int, Init, (int *) argc, (char ***) argv) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Init)(argc, argv);
  RETURN_TO_UPPER_HALF();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
USER_DEFINED_WRAPPER(int, Init_thread, (int *) argc, (char ***) argv,
                     (int) required, (int *) provided) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Init_thread)(argc, argv, required, provided);
  RETURN_TO_UPPER_HALF();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
#endif
// FIXME: See the comment in the wrapper function, defined
// later in the file.
// DEFINE_FNC(int, Finalize, (void))
DEFINE_FNC(double, Wtime, (void))
DEFINE_FNC(int, Finalized, (int *) flag)
DEFINE_FNC(int, Get_processor_name, (char *) name, (int *) resultlen)
DEFINE_FNC(int, Initialized, (int *) flag)

USER_DEFINED_WRAPPER(int, Finalize, (void))
{
  // FIXME: With Cray MPI, for some reason, MPI_Finalize hangs when
  // running on multiple nodes. The MPI Finalize call tries to pthread_cancel
  // a polling thread in the lower half and then blocks on join. But,
  // for some reason, the polling thread never comes out of an ioctl call
  // and remains blocked.
  //
  // The workaround here is to simply return success to the caller without
  // calling into the real Finalize function in the lower half. This way
  // the application can proceed to exit without getting blocked forever.
  return MPI_SUCCESS;
}

USER_DEFINED_WRAPPER(int, Get_count,
                     (const MPI_Status *) status, (MPI_Datatype) datatype,
                     (int *) count)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Get_count)(status, realType, count);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Init, int *argc, char ***argv)
PMPI_IMPL(int, MPI_Finalize, void)
PMPI_IMPL(int, MPI_Finalized, int *flag)
PMPI_IMPL(int, MPI_Get_processor_name, char *name, int *resultlen)
PMPI_IMPL(double, MPI_Wtime, void)
PMPI_IMPL(int, MPI_Initialized, int *flag)
PMPI_IMPL(int, MPI_Init_thread, int *argc, char ***argv,
          int required, int *provided)
PMPI_IMPL(int, MPI_Get_count, const MPI_Status *status, MPI_Datatype datatype,
          int *count)
