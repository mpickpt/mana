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
#include <vector>

#include <mpi.h>
#include <stdlib.h>

#include "dmtcp.h"

#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "lower_half_api.h"
#include "virtual-ids.h"
#include "mpi_nextfunc.h"

#define MAX_VIRTUAL_ID 999

// #define DEBUG_VIDS

// TODO I use an explicitly integer virtual id, which under macro
// reinterpretation will fit into an int64 pointer.  This should be
// fine if no real MPI Type is smaller than an int32.
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<unsigned int, ggid_desc_t*>::iterator ggid_desc_iterator;

// Per Yao Xu, MANA does not require the thread safety offered by
// DMTCP's VirtualIdTable. We use std::map.

// int vId -> id_desc_t*, which contains rId.
std::map<int, id_desc_t*> idDescriptorTable; 

// int ggid -> ggid_desc_t*, which contains CVC information.
std::map<unsigned int, ggid_desc_t*> ggidDescriptorTable;

// dead-simple vid generation mechanism, add one to get new ids.
int base = 1;
int nextvId = base;

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

// Compute the ggid [Global Group Identifier] of a real MPI communicator.
// This consists of a hash of its integer ranks.
// OUT: rbuf
// Returns ggid.
int getggid(MPI_Comm comm, int worldRank, int commSize, int* rbuf) {
  if (comm == MPI_COMM_NULL || comm == MPI_COMM_WORLD) {
    return comm;
  }
  unsigned int ggid = 0;

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Allgather)(&worldRank, 1, MPI_INT,
			rbuf, 1, MPI_INT, comm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  for (int i = 0; i < commSize; i++) {
    ggid ^= hash(rbuf[i] + 1);
  }

  return ggid;
}

// This is a descriptor initializer. Its job is to write an initial
// descriptor for a real MPI Communicator.

comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
  comm_desc_t* desc = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));

  desc->real_id = realComm;
  desc->global_ranks = NULL;

  return desc;
}

void grant_ggid(MPI_Comm virtualComm) {
  int worldRank, commSize, localRank;

  comm_desc_t* desc = ((comm_desc_t*)virtualToDescriptor(*((int*)&virtualComm)));

  MPI_Comm realComm = desc->real_id;

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_rank)(MPI_COMM_WORLD, &worldRank);
  NEXT_FUNC(Comm_size)(realComm, &commSize);
  NEXT_FUNC(Comm_rank)(realComm, &localRank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  int* ranks = ((int* )malloc(sizeof(int) * commSize));

  int ggid = getggid(realComm, worldRank, commSize, ranks);
  ggid_desc_iterator it = ggidDescriptorTable.find(ggid);

  if (it != ggidDescriptorTable.end()) {
    // There exists another communicator with the same ggid, i.e.,
    // this communicator is an alias. We use the same ggid_desc.
    desc->ggid_desc = it->second;
  } else {
    ggid_desc_t* gd = ((ggid_desc_t *) malloc(sizeof(ggid_desc_t)));
    gd->ggid = ggid;
    gd->target_num = 0;
    gd->seq_num = 0;
    ggidDescriptorTable[ggid] = gd;
    desc->ggid_desc = gd;
  }

  desc->global_ranks = ranks;
  desc->local_rank = localRank;
  desc->size = commSize;

  return;
}

// HACK See notes in virtual-ids.h about the reference counting.
void destroy_comm_desc_t(comm_desc_t* desc) {
  free(desc->global_ranks);
  // We DO NOT free the ggid_desc. Need to think about how to do it correctly.
  free(desc);
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  desc->global_ranks = NULL;
  desc->size = 0;
  return desc;
}

void destroy_group_desc_t(group_desc_t* group) {
  free(group->global_ranks);
  free(group);
}

request_desc_t* init_request_desc_t(MPI_Request realReq) {
  request_desc_t* desc = ((request_desc_t*)malloc(sizeof(request_desc_t)));
  desc->real_id = realReq;
  return desc;
}

void destroy_request_desc_t(request_desc_t* request) {
  free(request);
}

op_desc_t* init_op_desc_t(MPI_Op realOp) {
  op_desc_t* desc = ((op_desc_t*)malloc(sizeof(op_desc_t)));
  desc->real_id = realOp;
  desc->user_fn = NULL;
  return desc;
}

void destroy_op_desc_t(op_desc_t* op) {
    free(op);
}

datatype_desc_t* init_datatype_desc_t(MPI_Datatype realType) {
    datatype_desc_t* desc = ((datatype_desc_t*)malloc(sizeof(datatype_desc_t)));
    desc->real_id = realType;

    desc->num_integers = 0;
    desc->integers = NULL;

    desc->num_addresses = 0;
    desc->addresses = NULL;

    // FIXME: this was in Yao's spec sheet, but I haven't seen it used in any
    // of the relevant functions. Why is this here?
    desc->num_large_counts = 0;
    desc->large_counts = NULL;

    desc->num_datatypes = 0;
    desc->datatypes = NULL;

    desc->combiner = 0;
    desc->is_freed = false;
    return desc;
}

void destroy_datatype_desc_t(datatype_desc_t* datatype) {
  free(datatype->integers);
  free(datatype->addresses);
  free(datatype->large_counts);
  free(datatype->datatypes);
  free(datatype);
}

file_desc_t* init_file_desc_t(MPI_File realFile) {
  file_desc_t* desc = ((file_desc_t*)malloc(sizeof(file_desc_t)));
  desc->real_id = realFile;
  return desc;
}

void destroy_file_desc_t(file_desc_t* file) {
  free(file);
}

void print_id_descriptors() {
  printf("Printing %u id_descriptors:\n", idDescriptorTable.size());
  fflush(stdout);
  for (id_desc_pair pair : idDescriptorTable) {
    printf("%x\n", pair.first);
    fflush(stdout);
  }
}

// Given int virtualid, return the contained id_desc_t if it exists.
// Otherwise return NULL
id_desc_t* virtualToDescriptor(int virtId) {
#ifdef DEBUG_VIDS
  // print_id_descriptors();
#endif
  id_desc_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    return it->second;
  }
  return NULL;
}

// FIXME: We need to preemptively virtualize MPI_COMM_WORLD, with GGID. In the
// original code, there is also a pre-initialization for MPI_COMM_NULL, but
// that doesn't appear to be necessary for CVC algorithm functionality.
void init_comm_world() {
  comm_desc_t* comm_world = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));
  ggid_desc_t* comm_world_ggid = ((ggid_desc_t*)malloc(sizeof(ggid_desc_t)));
  comm_world->ggid_desc = comm_world_ggid;
  // The upper-half one.
  comm_world_ggid->ggid = MPI_COMM_WORLD;
  comm_world_ggid->seq_num = 0;
  comm_world_ggid->target_num = 0;

  // FIXME: This WILL NOT WORK when moving to OpenMPI, ExaMPI, as written.
  // Yao, Twinkle, should apply their lh_constants_map strategy here.
  comm_world->real_id = 0x84000000;
  // FIXME: the other fields are not initialized. This is an INTERNAL
  // communicator descriptor, strictly for bookkeeping, not for reconstructing,
  // etc.

  idDescriptorTable[MPI_COMM_WORLD] = ((union id_desc_t*)comm_world);
  ggidDescriptorTable[MPI_COMM_WORLD] = comm_world_ggid;
}
