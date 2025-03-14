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

#include <time.h>
#include "mpi.h"
#include "mpi_plugin.h"  // lh_info declared via lower_half_api.h
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "record-replay.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"
#include "p2p_drain_send_recv.h"
#include "mana_header.h"
#include "seq_num.h"
#include "uh_wrappers.h"

using namespace dmtcp_mpi;

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

ManaHeader g_mana_header = { .init_flag = MPI_INIT_NO_THREAD };

extern "C" {

int MPI_Init(int *argc, char ***argv) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();

  g_mana_header.init_flag = MPI_INIT_NO_THREAD;

  recordPreMpiInitMaps();
  recordPostMpiInitMaps();

  init_predefined_virt_ids();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  g_mana_header.init_flag = required;

  recordPreMpiInitMaps();
  recordPostMpiInitMaps();

  init_predefined_virt_ids();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Initialized(int *flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Initialized)(flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Finalized(int *flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Finalized)(flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Get_processor_name(char *name, int *resultlen)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_processor_name)(name, resultlen);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

double MPI_Wtime()
{
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  double nsec = (double)0.001 *0.001 *0.001;
  double ret = tp.tv_sec + tp.tv_nsec * nsec;
  return ret;
}

int MPI_Finalize(void)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Finalize)();
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Get_count(const MPI_Status *status, MPI_Datatype datatype, int *count)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = datatype}).datatype;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_count)(status, realType, count);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Get_library_version(char *version, int *resultlen)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_library_version)(version, resultlen);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int MPI_Get_address(const void *location, MPI_Aint *address)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_address)(location, address);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

// FOR DEBUGGING ONLY:
// This defines a call to MPI_MANA_Internal in the lower half, which
//   is especially useful in debugging restart.  It is called
//   from mpi-proxy-split/mpi_plugin.cpp, just before doing record-replay.
// In mpi-proxy-split/lower-half, redefine MPI_MANA_Internal()
//   to do whatever is desired.  Then do:
//   rm bin/lh_proxy
//   module list # Verify the correct 'module' settings
//   make -j mana
//   bin/mana_coordinator
//   bin/mana_restart
// If desired, add 'int dummy=1; shile(dummy);' inside MPI_MANA_Internal()
//   for interactive debbugging
int MPI_MANA_Internal(char *dummy)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(MANA_Internal)(dummy);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  if (retval != 0) {
    fprintf(stderr, "**** MPI_NANA_Internal returned: %d\n", retval);
    fflush(stdout);
  }
  return retval;
}

} // end of: extern "C"
