/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *   Edited 2023 by Leonid Belyaev                                          *
 *   belyaev.l@northeastern.edu                                             *
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
#include <map>

#include <mpi.h>
#include <stdlib.h>

#include "dmtcp.h"

#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "virtual-ids.h"

#define MAX_VIRTUAL_ID 999

typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable. We use std::map.
std::map<int, id_desc_t*> idDescriptorTable; // int vId -> id_desc_t*, which contains rId.
std::map<int, ggid_desc_t*> ggidDescriptorTable; // int ggid -> ggid_desc_t*

// vid generation mechanism.
int base = 1;
int nextvId = base;
// --- metadata structs ---

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

// Compute the ggid [Global Group Identifier] of a real MPI communicator.
// This consists of a hash of its integer ranks.
// Returns ggid.
int getggid(MPI_Comm comm) {
  if (comm == MPI_COMM_NULL) {
    return comm;
  }
  unsigned int ggid = 0;
  int worldRank, commSize;
  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
  MPI_Comm_size(comm, &commSize);
  int rbuf[commSize];

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Allgather)(&worldRank, 1, MPI_INT,
			rbuf, 1, MPI_INT, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  for (int i = 0; i < commSize; i++) {
    ggid ^= hash(rbuf[i] + 1);
  }

  return ggid;
}

comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
  int ggid = getggid(realComm);
  ggid_desc_iterator it = ggidDescriptorTable.find(ggid);
  comm_desc_t* desc = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));

  if (it != ggidDescriptorTable.end()) {
    desc->ggid_desc = it->second;
  } else {
    ggid_desc_t* gd = ((ggid_desc_t *) malloc(sizeof(ggid_desc_t)));
    gd->ggid = ggid;
    ggidDescriptorTable[ggid] = gd;
    desc->ggid_desc = gd;
  }
  desc->real_id = realComm;
  desc->size = 0;
  desc->local_rank = 0;
  desc->ranks = NULL;
  return desc;
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  desc->ranks = NULL;
  return desc;
}

request_desc_t* init_request_desc_t(MPI_Request realReq) {
  request_desc_t* desc = ((request_desc_t*)malloc(sizeof(request_desc_t)));
  desc->real_id = realReq;
  desc->request_kind = NULL;
  desc->status = 0;
  return desc;
}

op_desc_t* init_op_desc_t(MPI_Op realOp) {
  op_desc_t* desc = ((op_desc_t*)malloc(sizeof(op_desc_t)));
  desc->real_id = realOp;
  desc->user_fn = NULL;
  return desc;
}

datatype_desc_t* init_datatype_desc_t(MPI_Datatype realType) {
  datatype_desc_t* desc = ((datatype_desc_t*)malloc(sizeof(datatype_desc_t)));
  desc->real_id = realType;

  desc->num_integers = 0;
  desc->integers = NULL;
  desc->num_addresses = 0;
  desc->addresses = NULL;
  desc->num_large_counts = 0;
  desc->large_counts = NULL;
  desc->num_datatypes = 0;
  desc->datatypes = NULL;
  desc->combiner = NULL;
  return desc;
}

file_desc_t* init_file_desc_t(MPI_File realFile) {
  file_desc_t* desc = ((file_desc_t*)malloc(sizeof(file_desc_t)));
  desc->real_id = realFile;
  return desc;
}

id_desc_t* init_id_desc_t() {
  int vId = nextvId++;
  return NULL;
}

// Given id_desc_t, return the contained virtualid.
int descriptorToVirtual(id_desc_t* desc) {
  return ((comm_desc_t*)desc)->handle;
}

// Given int virtualid, return the contained id_desc_t if it exists.
// Otherwise return NULL
id_desc_t* virtualToDescriptor(int virtId) {
  id_desc_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    return it->second;
  }
  return NULL;
}

// Given int virtualId and realId of MPI size, update the descriptor referenced by virtualid, if it exists.
// TODO If the referenced virtualID does not exist, create a descriptor for it.
// Returns a reference to the descriptor created.
int updateMapping(int virtId, long realId) { // HACK MPICH
  id_desc_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    id_desc_t* desc = idDescriptorTable[virtId]; // if doesn't exist?
    ((comm_desc_t*)desc)->real_id = realId;
    return virtId;
  } else {
    // TODO
    // If doesn't exist, return null id.
    return 0;
  }
}

// Given id desc, get vid, fill out the vid handle, and map the desc.
// Returns the vId assigned.
int assignVid(id_desc_t* desc) {
  int vId = nextvId++; // TODO
  ((comm_desc_t*)desc)->handle = vId;

  idDescriptorTable[vId] = desc;
  return vId;
}

// Remove a descriptor by its virtualid.
// Returns the real id unmapped in the descriptor, which should fit into a long.
long onRemove(int virtId) {
  id_desc_t* desc;

  id_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    desc = it->second;
  }
  idDescriptorTable.erase(virtId);

  long realId = ((comm_desc_t *)vType)->real_id; // HACK MPICH
  free(vType);
  return realId;
}

// FIXME Using a function for this is not ideal. There's probably some macro magic we can use.
long virtual_to_real(id_desc_t* desc) {
  return (desc == NULL) ? NULL : ((comm_desc_t*)desc)->real_id
}
