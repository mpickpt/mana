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
bool g_libmpi_is_initialized = false;

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

#pragma weak MPI_Init = PMPI_Init
int PMPI_Init(int *argc, char ***argv) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();

  g_mana_header.init_flag = MPI_INIT_NO_THREAD;

  /*
   * The code below to Initialize MANA should be synchronized 
   *  with code section in PMPI_Init_thread code section below.
   *   
   * FIXME: This code section to Initialize MANA needs to 
   *    be moved to MANA's Plugin file. 
   * NOTE: But currently moving this code section casues segmentation 
   *    fault. This segmenation fault stems from execution of 
   *    function `init_predefined_virt_ids()` where updating
   *    `upper_to_lower_constants` maps results in comparision with a NULL pointer. 
   */
  recordPreMpiInitMaps();
  recordPostMpiInitMaps();

  init_predefined_virt_ids();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  g_libmpi_is_initialized = true;
  return retval;
}

#pragma weak MPI_Init_thread = PMPI_Init_thread
int PMPI_Init_thread(int *argc, char ***argv, int required, int *provided) {
  int retval;
  if (isUsingCollectiveToP2p()) {
    fprintf(stderr, collective_p2p_string);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  g_mana_header.init_flag = required;

  /*
   * The code below to Initialize MANA should be synchronized 
   *  with code section in PMPI_Init code section above.
   *   
   * FIXME: This code section to Initialize MANA needs to 
   *    be moved to MANA's Plugin file. 
   * NOTE: But currently moving this code section casues segmentation 
   *    fault. This segmenation fault stems from execution of 
   *    function `init_predefined_virt_ids()` where updating
   *    `upper_to_lower_constants` maps results in comparision with a NULL pointer. 
   */
  recordPreMpiInitMaps();
  recordPostMpiInitMaps();

  init_predefined_virt_ids();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  g_libmpi_is_initialized = true;
  return retval;
}

#pragma weak MPI_Initialized = PMPI_Initialized
int PMPI_Initialized(int *flag)
{
  int retval;
  if (g_libmpi_is_initialized && g_libmana_is_initialized) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    retval = NEXT_FUNC(Initialized)(flag);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }
  else {
    *flag = 0;
    return 0;
  } 
}

#pragma weak MPI_Finalized = PMPI_Finalized
int PMPI_Finalized(int *flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Finalized)(flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Get_processor_name = PMPI_Get_processor_name
int PMPI_Get_processor_name(char *name, int *resultlen)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_processor_name)(name, resultlen);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Wtime = PMPI_Wtime
double PMPI_Wtime()
{
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  double nsec = (double)0.001 *0.001 *0.001;
  double ret = tp.tv_sec + tp.tv_nsec * nsec;
  return ret;
}

#pragma weak MPI_Finalize = PMPI_Finalize
int PMPI_Finalize(void)
{
  int retval;
  /*
   *  When calling MPI_Finalize, we disable checkpointing
   *    and do not enable it back up. 
   *  Rationale: Some MPI applications have special code sections 
   *    that run for specific ranks only. 
   *    For example, `rank = 0` is generally seen to print out output of 
   *      Collective Communication calls, such as MPI_Reduce. 
   *      Therefore, since the rest of the ranks  exit before 
   *      `rank = 0` process, `interval` based checkpointing can create 
   *      corrupt ckpt_images, which give segmentation fault on restart.
   */
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Finalize)();
  RETURN_TO_UPPER_HALF();
  return retval;
}

#pragma weak MPI_Get_count = PMPI_Get_count
int PMPI_Get_count(const MPI_Status *status, MPI_Datatype datatype, int *count)
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

#pragma weak MPI_Get_library_version = PMPI_Get_library_version
int PMPI_Get_library_version(char *version, int *resultlen)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  retval = NEXT_FUNC(Get_library_version)(version, resultlen);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#pragma weak MPI_Get_address = PMPI_Get_address
int PMPI_Get_address(const void *location, MPI_Aint *address)
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
