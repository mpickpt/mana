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

#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "virtual-ids.h"
#include "lower_half_api.h"

// #include "jassert.h"
// #include "kvdb.h"
// #include "seq_num.h"
// #include "mpi_nextfunc.h"
// #include "virtual-ids.h"
// #include "record-replay.h"



// may be needed?
// using namespace dmtcp_mpi;

#define MAX_VIRTUAL_ID 999

// TODO

typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;

// TODO Go through the descriptors one by one and see what's up.
// Allgather, local<->global rank function old ggid code
// seq_num

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable. We use std::map.
std::map<int, id_desc_t*> idDescriptorTable; // int vId -> id_desc_t*, which contains rId.
std::map<int, ggid_desc_t*> ggidDescriptorTable; // int ggid -> ggid_desc_t*, which contains CVC information.

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
int getggid(MPI_Comm comm, int worldRank, int commSize, int* rbuf) {
  if (comm == MPI_COMM_NULL || comm == MPI_COMM_WORLD) {
    return comm;
  }
  unsigned int ggid = 0;
  // int worldRank, commSize;
  // MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
  // MPI_Comm_size(comm, &commSize);
  // int rbuf[commSize];

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

// This is a descriptor initializer. Its job is to write a descriptor for a real MPI Communicator.
// This means making MPI calls that obtain metadata about this MPI communicator.
comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
    int worldRank, commSize, localRank;

  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
  MPI_Comm_size(realComm, &commSize);
  MPI_Comm_rank(realComm, &localRank);

  int* ranks = ((int* )malloc(sizeof(int) * commSize));

  int ggid = getggid(realComm, worldRank, commSize, ranks);
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
  // desc->size = commSize;
  // desc->local_rank = localRank;

  // desc->ranks = ranks;
  return desc;
}

// This is a communicator descriptor updater.
// Its job is to fill out the descriptor with all relevant metadata information (typically at precheckpoint time),
// which allows for O(1) reconstruction of the real id at restart time.

// TODO This may be inefficient because it causes repeated reconstruction of the respective group.
// A more efficient design would link groups and comms, but would introduce complexity.
void update_comm_desc_t(comm_desc_t* desc) {
  MPI_Group group;
  MPI_Comm_group(desc->real_id, &group);
  int groupSize;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(group, &groupSize);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  int* local_ranks = ((int*)malloc(sizeof(int) * groupSize));
  int* global_ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    local_ranks[i] = i;
  }

  MPI_Group_translate_ranks(group, groupSize, local_ranks, g_world_group, global_ranks);

  desc->ranks = global_ranks;
  desc->size = groupSize;
}

// This is a communicator descriptor reconstructor.
// Its job is to take the metadata of the descriptor and re-create the communicator it describes.
// It is typically called at restart time.
void reconstruct_with_comm_desc_t(comm_desc_t* desc) {
  MPI_Group group;
  MPI_Group_incl(g_world_group, desc->size, desc->ranks, group);
  MPI_Comm_create_group(MPI_COMM_WORLD, group, 0, &desc->real_id);
}

// This is a communicator descriptor destructor.
// Its job is to free the communicator descriptor (but NOT the real communicator, or related, itself)
void destroy_comm_desc_t(comm_desc_t* desc) {
  free(desc->ranks);
  ggid_desc_t* tmp = desc->ggid_desc;
  free(desc);
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  int groupSize;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(realGroup, &groupSize);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  int* ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    ranks[i] = i;
  }
  desc->ranks = ranks;
  return desc;
}

// Translate the local ranks of this group to global ranks, which are unique.
void update_group_desc_t(group_desc_t* group) {
  int groupSize;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(realGroup, &groupSize);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  int* local_ranks = ((int*)malloc(sizeof(int) * groupSize));
  int* global_ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    ranks[i] = i;
  }

  MPI_Group_translate_ranks(group->real_id, groupSize, local_ranks, g_world_group, global_ranks);

  group->ranks = global_ranks;
  group->size = groupSize;
}

void reconstruct_with_group_desc_t(group_desc_t* group) {
  MPI_Group_incl(g_world_group, group->size, group->ranks, &group->real_id);
}

void destroy_group_desc_t(group_desc_t* group) {
  free(group->ranks);
  free(group);
}

request_desc_t* init_request_desc_t(MPI_Request realReq) {
  request_desc_t* desc = ((request_desc_t*)malloc(sizeof(request_desc_t))); // FIXME
  desc->real_id = realReq;
  // desc->request_kind = NULL; Maybe not needed
			       // MPI_Request_get_status(realReq, NULL, desc->status);
  // TODO no need to init status (MPI standard)
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

void update_op_desc_t(op_desc_t* op) {
  // Nothing needs to be done here, all needed information is acquired at creation time.
}

void reconstruct_with_op_desc_t(op_desc_t* op) {
  MPI_Op_create(op->user_fn, op->commute, &op->real_id);
}

void destroy_op_desc_t(op_desc_t* op) {
  // free(op->user_fn); // TODO
  free(op);
}

// TODO mana_launch -i SECONDS
// Or mana_coordinator

datatype_desc_t* init_datatype_desc_t(MPI_Datatype realType) {
  datatype_desc_t* desc = ((datatype_desc_t*)malloc(sizeof(datatype_desc_t)));
  desc->real_id = realType;
  // 5.1.13 decoding a datatype
  // MPI_TYPE_GET_ENVELOPE pass to
  // MPI_Type_get_contents

  desc->num_integers = 0;
  desc->integers = NULL;

  desc->num_addresses = 0;
  desc->addresses = NULL;

  desc->num_large_counts = 0;
  desc->large_counts = NULL;

  desc->num_datatypes = 0;
  desc->datatypes = NULL;

  desc->combiner = NULL; // TODO ?
  return desc;
}

void update_datatype_desc_t(datatype_desc_t* datatype) {
  MPI_Type_get_envelope(datatype->real_id, &datatype->num_integers, &datatype->num_addresses, &datatype->num_datatypes, &datatype->combiner); // Get the sizes of each array...
  MPI_Type_get_contents(datatype->real_id, datatype->num_integers, datatype->num_addresses, datatype->num_datatypes, datatype->integers, datatype->addresses, datatype->datatypes);  // And get the contents of each array.
}

// TODO ensure I understand this correctly.
void reconstruct_with_datatype_desc_t(datatype_desc_t* datatype) {
  int count = datatype->num_integers + datatype->num_addresses + datatype->num_datatypes;
  MPI_Type_create_struct(count, datatype->integers, datatype->addresses, datatype->datatypes, &datatype->real_id);
}

void destroy_datatype_desc_t(datatype_desc_t* datatype) {
  free(datatype->integers); // TODO
  free(datatype->addresses);
  free(datatype->large_counts);
  free(datatype->datatypes);
  free(datatype->combiner);
  free(datatype);
}

// srun -n PROC cmd

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
    printf("%i\n", pair.first);
    printf("%u %i\n", ((comm_desc_t*)pair.second)->real_id, ((comm_desc_t*)pair.second)->real_id); // HACK
    fflush(stdout);
  }
}

// Given int virtualid, return the contained id_desc_t if it exists.
// Otherwise return NULL
id_desc_t* virtualToDescriptor(int virtId) {
  // print_id_descriptors();
  id_desc_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    return it->second;
  }
  return NULL;
}

void init_comm_world() {
  comm_desc_t* comm_world = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));
  comm_world->real_id = MPI_COMM_WORLD;
  ggid_desc_t* comm_world_ggid = ((ggid_desc_t*)malloc(sizeof(ggid_desc_t)));
  comm_world->ggid_desc = comm_world_ggid;
  comm_world_ggid->ggid = MPI_COMM_WORLD;
  comm_world_ggid->seq_num = 0;
  comm_world_ggid->target_num = 0;
  idDescriptorTable[MPI_COMM_WORLD] = ((union id_desc_t*)comm_world);
}

// For all descriptors, update the respective information.
void update_descriptors() {
  // iterate through descriptors. Determine the type from the vid_mask that we set in ADD_NEW.
  for (id_desc_pair pair : idDescriptorTable) {
    // Grab the first byte
    switch (pair.first & 0xFF000000) { 
      case UNDEFINED_MASK:
        break;
      case COMM_MASK:
	update_comm_desc_t((comm_desc_t*)pair.second);
	break;
      case GROUP_MASK:
	update_group_desc_t((group_desc_t*)pair.second);
	break;
      case REQUEST_MASK:
	update_request_desc_t((request_desc_t*)pair.second);
	break;
      case OP_MASK:
	update_op_desc_t((op_desc_t*)pair.second);
	break;
      case DATATYPE_MASK:
	update_datatype_desc_t((datatype_desc_t*)pair.second);
	break;
      case FILE_MASK:
	update_file_desc_t((file_desc_t*)pair.second);
	break;
      case COMM_KEYVAL_MASK:
	update_comm_keyval_desc_t((comm_keyval_desc_t*)pair.second);
	break;
    }
  }
}
