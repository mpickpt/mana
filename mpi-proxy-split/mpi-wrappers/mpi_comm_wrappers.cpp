/****************************************************************************
 *   Copyright (C) 2019-2022 by Gene Cooperman, Illio Suardi, Rohan Garg,   *
 *   Yao Xu                                                                 *
 *   gene@ccs.neu.edu, illio@u.nus.edu, rohgarg@ccs.neu.edu,                *
 *   xu.yao1@northeastern.edu                                               *
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

#include <unordered_map>
#include <vector>

#include <mpi.h>
#include "mpi_plugin.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_nextfunc.h"
#include "virtual-ids.h"
#include "seq_num.h"
#include "p2p_drain_send_recv.h"


// TODO
// - validate operation status (right now we assume them to be successful by
//   default)
// - immediately abort if comm == MPI_NULL_COMM in any of those cases

// We implement our own data structure that bypasses the lower half for:
// * MPI_Comm_create_keyval
// * MPI_Comm_get_attr
// * MPI_Comm_set_attr
// * MPI_Comm_delete_attr
// * MPI_Comm_free

// The general workflow goes:
// [create keyval] -> [get/set/delete attributes for keyval] -> [free keyval]
// Attribute keys are locally unique in processes, and are used to associate
// and access attributes on any locally defined communicator. This means that
// multiple communicators can use the same attribute key for different
// attributes. Thus, we use a structure like the following:
// [keyval -> {[communicator -> attribute], extra_state, copy_fn, delete_fn}]
struct KeyvalTuple {
  KeyvalTuple () = default;

  KeyvalTuple (void *extraState,
           MPI_Comm_copy_attr_function *copyFn,
           MPI_Comm_delete_attr_function *deleteFn,
           std::unordered_map<MPI_Comm, void*> attributeMap)
    : _extraState(extraState),
      _copyFn(copyFn),
      _deleteFn(deleteFn),
      _attributeMap(attributeMap) {};
  void *_extraState;
  MPI_Comm_copy_attr_function *_copyFn;
  MPI_Comm_delete_attr_function *_deleteFn;
  std::unordered_map<MPI_Comm, void*> _attributeMap;
};

static std::vector<int> keyvalVec;
static std::unordered_map<int, KeyvalTuple> tupleMap;

// The following are prvalues. So we need an address to point them to.
// On Cori, this is 2^22 - 1, but it just needs to be greater than 2^15 - 1.
static const int MAX_TAG_COUNT = 2097151;
// We're actually supposed to check the rank of the HOST process in the group
// associated with the MPI_COMM_WORLD communicator, but even the standard says
// "MPI does not specify what it means for a process to be a HOST, nor does it
// require that a HOST exists". So we just use a no HOST value.
static const int MPI_HOST_RANK = MPI_PROC_NULL;
static const int MPI_IO_SOURCE = MPI_ANY_SOURCE;
static const int MPI_WTIME_IS_GLOBAL_VAL = 0;

USER_DEFINED_WRAPPER(int, Comm_size, (MPI_Comm) comm, (int *) world_size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_size)(realComm, world_size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_rank, (MPI_Comm) comm, (int *) world_rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_rank)(realComm, world_rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create, (MPI_Comm) comm, (MPI_Group) group,
                     (MPI_Comm *) newcomm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Comm_create)(realComm, realGroup, newcomm);
    RETURN_TO_UPPER_HALF();
    if (retval == MPI_SUCCESS) {
      MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
      // unsigned int gid = VirtualGlobalCommId::instance() TODO
      //.createGlobalId(virtComm);
      *newcomm = virtComm;
      active_comms.insert(virtComm);
      // std::map<unsigned int, unsigned long>::iterator it =
      //   seq_num.find(gid);
      // if (it == seq_num.end()) {
      //   seq_num[gid] = 0;
      //   target[gid] = 0;
      // }
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Abort, (MPI_Comm) comm, (int) errorcode)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Abort)(realComm, errorcode);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_compare,
                     (MPI_Comm) comm1, (MPI_Comm) comm2, (int*) result)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm1 = VIRTUAL_TO_REAL_COMM(comm1);
  MPI_Comm realComm2 = VIRTUAL_TO_REAL_COMM(comm2);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_compare)(realComm1, realComm2, result);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int
MPI_Comm_free_internal(MPI_Comm *comm)
{
  int retval;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(*comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_free)(&realComm);
  RETURN_TO_UPPER_HALF();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_free, (MPI_Comm *) comm)
{
  // This bit of code is to execute the delete callback function when
  // MPI_Comm_free is called. Typically we call this function for each
  // attribute, but since our structure here is a bit different, we check, for
  // each key value, if a communicator/attribute value pairing exists, and if
  // it does then we call the callback function.
  for (auto &tuplePair : tupleMap) {
    KeyvalTuple *tuple = &tuplePair.second;
    std::unordered_map<MPI_Comm, void*> *attributeMap = &tuple->_attributeMap;
    if (attributeMap->find(*comm) != attributeMap->end()) {
      if (tuple->_deleteFn != MPI_COMM_NULL_DELETE_FN) {
        tuple->_deleteFn(*comm,
                         tuplePair.first,
                         attributeMap->at(*comm),
                         tuple->_extraState);
      }
      attributeMap->erase(*comm);
    }
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = MPI_Comm_free_internal(comm);
  if (retval == MPI_SUCCESS) {
    // NOTE: We cannot remove the old comm from the map, since
    // we'll need to replay this call to reconstruct any other comms that
    // might have been created using this comm.
    //
    // 2023-08-07 No longer.
    REMOVE_OLD_COMM(*comm);
    active_comms.erase(*comm);

    // unsigned int gid = VirtualGlobalCommId::instance().getGlobalId(*comm); TODO TODO
    // This appears to be unnecessary, given the "if 0".
#if 0 
    seq_num.erase(gid);
#endif
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_get_attr, (MPI_Comm) comm,
                     (int) comm_keyval, (void *) attribute_val, (int *) flag)
{
  int retval = MPI_SUCCESS;
  *flag = 0;
  if (comm == MPI_COMM_NULL) {
    return MPI_ERR_COMM;
  }
  // Environmental inquiries
  switch (comm_keyval) {
    case MPI_TAG_UB:
      * (const int **) attribute_val = &MAX_TAG_COUNT;
      *flag = 1;
      return retval;
    case MPI_HOST:
      * (const int **) attribute_val = &MPI_HOST_RANK;
      *flag = 1;
      return retval;
    case MPI_IO:
      * (const int **) attribute_val = &MPI_IO_SOURCE;
      *flag = 1;
      return retval;
    case MPI_WTIME_IS_GLOBAL:
      * (const int **) attribute_val = &MPI_WTIME_IS_GLOBAL_VAL;
      *flag = 1;
      return retval;
  }

  // Regular queries
  if (tupleMap.find(comm_keyval) == tupleMap.end()) {
    return MPI_ERR_KEYVAL;
  }
  KeyvalTuple *tuple = &tupleMap.at(comm_keyval);
  std::unordered_map<MPI_Comm, void*> *attributeMap = &tuple->_attributeMap;
  if (attributeMap->find(comm) != attributeMap->end()) {
    * (void **) attribute_val = attributeMap->at(comm);
    * flag = 1;
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_set_attr, (MPI_Comm) comm,
                     (int) comm_keyval, (void *) attribute_val)
{
  int retval = MPI_SUCCESS;
  if (comm == MPI_COMM_NULL) {
    retval = MPI_ERR_COMM;
    return retval;
  }
  if (tupleMap.find(comm_keyval) == tupleMap.end()) {
    retval = MPI_ERR_KEYVAL;
    return retval;
  }
  KeyvalTuple *tuple = &tupleMap.at(comm_keyval);
  std::unordered_map<MPI_Comm, void*> *attributeMap = &tuple->_attributeMap;
  if (attributeMap->find(comm) != attributeMap->end()) {
    if (tuple->_deleteFn != MPI_COMM_NULL_DELETE_FN) {
      tuple->_deleteFn(comm,
                       comm_keyval,
                       attributeMap->at(comm),
                       tuple->_extraState);
    }
    attributeMap->erase(comm);
  }
  attributeMap->emplace(comm, attribute_val);
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_delete_attr, (MPI_Comm) comm, (int) comm_keyval)
{
  int retval = MPI_SUCCESS;
  if (comm == MPI_COMM_NULL) {
    retval = MPI_ERR_COMM;
    return retval;
  }
  if (tupleMap.find(comm_keyval) == tupleMap.end()) {
    return retval;
  }
  KeyvalTuple *tuple = &tupleMap.at(comm_keyval);
  std::unordered_map<MPI_Comm, void*> *attributeMap = &tuple->_attributeMap;
  if (attributeMap->find(comm) != attributeMap->end()) {
    if (tuple->_deleteFn != MPI_COMM_NULL_DELETE_FN) {
      tuple->_deleteFn(comm,
                       comm_keyval,
                       attributeMap->at(comm),
                       tuple->_extraState);
    }
    attributeMap->erase(comm);
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_set_errhandler,
                     (MPI_Comm) comm, (MPI_Errhandler) errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_set_errhandler)(realComm, errhandler);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Topo_test,
                     (MPI_Comm) comm, (int *) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Topo_test)(realComm, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_split_type, (MPI_Comm) comm, (int) split_type,
                     (int) key, (MPI_Info) inf, (MPI_Comm*) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_split_type)(realComm, split_type, key, inf, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    // VirtualGlobalCommId::instance().createGlobalId(virtComm); TODO
    *newcomm = virtComm;
    active_comms.insert(virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create_keyval,
                     (MPI_Comm_copy_attr_function *) comm_copy_attr_fn,
                     (MPI_Comm_delete_attr_function *) comm_delete_attr_fn,
                     (int *) comm_keyval, (void *) extra_state)
{
  int retval = MPI_SUCCESS;
  int keyval = 0;
  for (; keyval < keyvalVec.size(); keyval++) {
    if (keyval == MPI_ERR_KEYVAL) {
      continue;
    }
    if (keyvalVec[keyval] == -1) {
      keyvalVec[keyval] = keyval;
      *comm_keyval = keyvalVec[keyval];
      tupleMap.emplace(keyval, KeyvalTuple(
                       extra_state,
                       comm_copy_attr_fn,
                       comm_delete_attr_fn,
                       std::unordered_map<MPI_Comm, void*>()));
      return retval;
    }
  }
  keyvalVec.push_back(keyval);
  *comm_keyval = keyvalVec[keyval];
  tupleMap.emplace(keyval, KeyvalTuple(
                   extra_state,
                   comm_copy_attr_fn,
                   comm_delete_attr_fn,
                   std::unordered_map<MPI_Comm, void*>()));
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_free_keyval, (int *) comm_keyval)
{
  int retval = MPI_SUCCESS;
  int keyval = *comm_keyval;
  if (keyval < keyvalVec.size() && keyvalVec[keyval] != -1) {
    keyvalVec[keyval] = -1;
    tupleMap.erase(keyval);
  } else {
    JWARNING(false)(keyval).Text("Attempted to free an invalid key!");
  }
  return retval;
}

int
MPI_Comm_create_group_internal(MPI_Comm comm, MPI_Group group, int tag,
                               MPI_Comm *newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_create_group)(realComm, realGroup, tag, newcomm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create_group, (MPI_Comm) comm,
                     (MPI_Group) group, (int) tag, (MPI_Comm *) newcomm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval = MPI_Comm_create_group_internal(comm, group, tag, newcomm);
    if (retval == MPI_SUCCESS) {
      MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
      // VirtualGlobalCommId::instance().createGlobalId(virtComm); TODO
      *newcomm = virtComm;
      active_comms.insert(virtComm);
    }
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

PMPI_IMPL(int, MPI_Comm_size, MPI_Comm comm, int *world_size)
PMPI_IMPL(int, MPI_Comm_rank, MPI_Comm comm, int *world_rank)
PMPI_IMPL(int, MPI_Abort, MPI_Comm comm, int errorcode)
PMPI_IMPL(int, MPI_Comm_create, MPI_Comm comm, MPI_Group group,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_compare, MPI_Comm comm1, MPI_Comm comm2, int *result)
PMPI_IMPL(int, MPI_Comm_free, MPI_Comm *comm)
PMPI_IMPL(int, MPI_Comm_get_attr, MPI_Comm comm, int comm_keyval,
          void *attribute_val, int *flag)
PMPI_IMPL(int, MPI_Comm_set_attr, MPI_Comm comm, int comm_keyval,
          void *attribute_val)
PMPI_IMPL(int, MPI_Comm_set_errhandler, MPI_Comm comm,
          MPI_Errhandler errhandler)
PMPI_IMPL(int, MPI_Topo_test, MPI_Comm comm, int* status)
PMPI_IMPL(int, MPI_Comm_split_type, MPI_Comm comm, int split_type, int key,
          MPI_Info info, MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_create_keyval,
          MPI_Comm_copy_attr_function * comm_copy_attr_fn,
          MPI_Comm_delete_attr_function * comm_delete_attr_fn,
          int *comm_keyval, void *extra_state)
PMPI_IMPL(int, MPI_Comm_free_keyval, int *comm_keyval)
PMPI_IMPL(int, MPI_Comm_create_group, MPI_Comm comm, MPI_Group group,
          int tag, MPI_Comm *newcomm)
