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

#include "seq_num.h"

#define MAX_VIRTUAL_ID 999

// #define DEBUG_VIDS

// TODO I use an explicitly integer virtual id, which under macro reinterpretation will fit into an int64 pointer.
// This should be fine if no real MPI Type is smaller than an int32.
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable. We use std::map.
std::map<int, id_desc_t*> idDescriptorTable; // int vId -> id_desc_t*, which contains rId.
std::map<int, ggid_desc_t*> ggidDescriptorTable; // int ggid -> ggid_desc_t*, which contains CVC information.

// vid generation mechanism.
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

// This is a descriptor initializer. Its job is to write an initial descriptor for a real MPI Communicator.
comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
    int worldRank, commSize, localRank;
#ifdef DEBUG_VIDS
    printf("init_comm_desc_t realComm: %x\n", realComm);
    fflush(stdout);
#endif

  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_size)(realComm, &commSize);
  NEXT_FUNC(Comm_rank)(realComm, &localRank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

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
  return desc;
}

// This is a communicator descriptor updater.
// Its job is to fill out the descriptor with all relevant metadata information (typically at precheckpoint time),
// which allows for O(1) reconstruction of the real id at restart time.

// TODO This may be inefficient because it causes repeated reconstruction of the respective group.
// A more efficient design would link groups and comms, but would introduce complexity.
void update_comm_desc_t(comm_desc_t* desc) {
  MPI_Group group;

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(desc->real_id, &group);
  RETURN_TO_UPPER_HALF();


#ifdef DEBUG_VIDS
  printf("update_comm_desc group: %x\n", group);
  fflush(stdout);
#endif
  int groupSize = 0;

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(group, &groupSize);
  RETURN_TO_UPPER_HALF();

  desc->size = groupSize;

  int* local_ranks = ((int*)malloc(sizeof(int) * groupSize));
  int* global_ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    local_ranks[i] = i;
  }

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_translate_ranks)(group, groupSize, local_ranks, g_world_group, global_ranks);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  desc->ranks = global_ranks;
}

// This is a communicator descriptor reconstructor.
// Its job is to take the metadata of the descriptor and re-create the communicator it describes.
// It is typically called at restart time.
void reconstruct_with_comm_desc_t(comm_desc_t* desc) {
#ifdef DEBUG_VIDS
  printf("reconstruct_comm_desc_t comm: %x -> %x\n", desc->handle, desc->real_id);

  fflush(stdout);
  printf("reconstruct_with_comm_desc_t comm size: %x\n", desc->size);

  fflush(stdout);
  printf("reconstruct_with_comm_desc_t ranks:");

  fflush(stdout);
  for (int i = 0; i < desc->size; i++) {
    printf(" %i", desc->ranks[i]);
  }
  printf("\n");

  fflush(stdout);
  fflush(stdout);
#endif
  // HACK MPI_COMM_WORLD in the LH.
  if (desc->real_id == 0x84000000) { 
    return;
  }

  // We recreate the communicator with the reconstructed group and MPI_COMM_WORLD.

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  MPI_Group group;
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->ranks, &group);
  NEXT_FUNC(Comm_create_group)(MPI_COMM_WORLD, group, 0, &desc->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
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
#ifdef DEBUG_VIDS
  printf("init_group_desc_t: %x\n", realGroup);
  fflush(stdout);
#endif
  MPI_Group_size(realGroup, &groupSize);
  int* ranks = ((int*)malloc(sizeof(int) * groupSize));
  int* global_ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    ranks[i] = i;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_translate_ranks)(realGroup, groupSize, ranks, g_world_group, global_ranks);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  desc->ranks = global_ranks;
  desc->size = groupSize;

  return desc;
}

// Translate the local ranks of this group to global ranks, which are unique.
void update_group_desc_t(group_desc_t* group) {
  // TODO What happens when we call for a checkpoint while a checkpoint is already occurring? 
  DMTCP_PLUGIN_DISABLE_CKPT();
  int groupSize;
#ifdef DEBUG_VIDS
  printf("update_group_desc_t group: %x\n", group->real_id);
  fflush(stdout);
#endif

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(group->real_id, &groupSize);
  RETURN_TO_UPPER_HALF();

  int* local_ranks = ((int*)malloc(sizeof(int) * groupSize));
  int* global_ranks = ((int*)malloc(sizeof(int) * groupSize));
  for (int i = 0; i < groupSize; i++) {
    local_ranks[i] = i;
  }

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_translate_ranks)(group->real_id, groupSize, local_ranks, g_world_group, global_ranks);
  RETURN_TO_UPPER_HALF();

  group->ranks = global_ranks;
  group->size = groupSize;
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void reconstruct_with_group_desc_t(group_desc_t* group) {
#ifdef DEBUG_VIDS
  printf("reconstruct_with_group_desc_t group: %x -> %x\n", group->handle, group->real_id);
  printf("reconstruct_with_group_desc_t group size: %x\n", group->size);
  printf("reconstruct_with_group_desc_t ranks:");
  for (int i = 0; i < group->size; i++) {
    printf(" %i", group->ranks[i]);
  }
  printf("\n");
  printf("reconstruct_with_group_desc_t g_world_group: %x\n", g_world_group);
  fflush(stdout);
#endif
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, group->size, group->ranks, &group->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void destroy_group_desc_t(group_desc_t* group) {
  free(group->ranks);
  free(group);
}

// TODO Currently not supported.
// "The support of non-blocking collective commmunication is still not merged into the main branch.
// But you can ignore MPI Request for now."
request_desc_t* init_request_desc_t(MPI_Request realReq) {
  request_desc_t* desc = ((request_desc_t*)malloc(sizeof(request_desc_t))); // FIXME
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

// This update function is special, for the information we need is only available at creation time, and not with MPI calls.
// We manually invoke this function at creation time.
void update_op_desc_t(op_desc_t* op, MPI_User_function* user_fn, int commute) {
  op->user_fn = user_fn;
  op->commute = commute;
}

void reconstruct_with_op_desc_t(op_desc_t* op) {
  MPI_Op_create(op->user_fn, op->commute, &op->real_id);
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

  desc->num_large_counts = 0;
  desc->large_counts = NULL;

  desc->num_datatypes = 0;
  desc->datatypes = NULL;

  desc->combiner = 0;
  return desc;
}

void update_datatype_desc_t(datatype_desc_t* datatype) {
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Type_get_envelope)(datatype->real_id, &datatype->num_integers, &datatype->num_addresses, &datatype->num_datatypes, &datatype->combiner);
  datatype->integers = ((int*)malloc(sizeof(int) * datatype->num_integers));
  datatype->addresses = ((MPI_Aint*)malloc(sizeof(MPI_Aint) * datatype->num_addresses));
  datatype->datatypes = ((MPI_Datatype*)malloc(sizeof(MPI_Datatype) * datatype->num_datatypes));
  NEXT_FUNC(Type_get_contents)(datatype->real_id, datatype->num_integers, datatype->num_addresses, datatype->num_datatypes, datatype->integers, datatype->addresses, datatype->datatypes);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void reconstruct_with_datatype_desc_t(datatype_desc_t* datatype) {
  // TODO this probably needs a switch statement over COMBINER
  // Type_vector_test has: datatype: num_integers: 2, num_addresses: 1, num_datatypes: 1  
  // But MPI_Type_create_struct expects an equal amount of each. This doesn't work.
  // Our datatype is created like:
  // MPI_Type_vector(BUFFER_SIZE, 1, BUFFER_SIZE, MPI_INIT, &column_type);
  // Where column_type is outparam.
  // BUFFER_SIZE is 100 here.
  // Count, blocklen, stride, type.
  // Here, we might say that count, blocklen are int, stride is addr, type is type. 2 1 1.
  int count = datatype->num_integers + datatype->num_addresses + datatype->num_datatypes;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  switch (datatype->combiner) {
	  case MPI_COMBINER_VECTOR:
		  NEXT_FUNC(Type_vector)(datatype->integers[0], datatype->integers[1], datatype->addresses[0], datatype->datatypes[0], &datatype->real_id);
		  break;
	  case MPI_COMBINER_STRUCT:
		  // here, all arrays have equal len.
  		  NEXT_FUNC(Type_create_struct)(datatype->num_integers, datatype->integers, datatype->addresses, datatype->datatypes, &datatype->real_id);
		  break;
	  case MPI_COMBINER_HVECTOR:
		  // TODO just a guess.
		  NEXT_FUNC(Type_hvector)(datatype->integers[0], datatype->integers[1], datatype->addresses[0], datatype->datatypes[0], &datatype->real_id);
		  break;
          case MPI_COMBINER_INDEXED:
	    // Here, the integers in the envelope are split into two arrays.
	    int count = datatype->num_integers / 2;
                  NEXT_FUNC(Type_indexed)(count, datatype->integers, datatype->integers + count, datatype->datatypes[0], &datatype->real_id);
                  break;
		  //case MPI_COMBINER_HINDEXED:
                  //NEXT_FUNC(Type_indexed)(datatype->num_integers, datatype->integers, datatypes->addresses, datatype->datatypes[0], &datatype->real_id);
                  //break;
	  default:
		  break;
  }
  // TODO is this needed?
  NEXT_FUNC(Type_commit)(&datatype->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
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
  print_id_descriptors();
#endif
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
#ifdef DEBUG_VIDS
  printf("update_descriptors\n");
  fflush(stdout);
#endif
  for (id_desc_pair pair : idDescriptorTable) {
    switch (pair.first & 0xFF000000) { 
      case UNDEFINED_MASK:
        break;
      case COMM_MASK:
#ifdef DEBUG_VIDS
	printf("update_comm -> %x\n", pair.first);
	fflush(stdout);
#endif
	update_comm_desc_t((comm_desc_t*)pair.second);
	break;
      case GROUP_MASK:
#ifdef DEBUG_VIDS
	printf("update_group\n");
	fflush(stdout);
#endif
	update_group_desc_t((group_desc_t*)pair.second);
	break;
      case REQUEST_MASK:
	// update_request_desc_t((request_desc_t*)pair.second);
	break;
      case OP_MASK:
	// update_op_desc_t((op_desc_t*)pair.second);
	// HACK: This must be called on init. see update_op_desc
	break;
      case DATATYPE_MASK:
#ifdef DEBUG_VIDS
	printf("update_datatype\n");
	fflush(stdout);
#endif
	update_datatype_desc_t((datatype_desc_t*)pair.second);
	break;
      case FILE_MASK:
	// update_file_desc_t((file_desc_t*)pair.second);
	break;
      case COMM_KEYVAL_MASK:
	// update_comm_keyval_desc_t((comm_keyval_desc_t*)pair.second);
	break;
      default:
	break;
    }
  }
}

void prepare_reconstruction() {
#ifdef DEBUG_VIDS
  printf("Prepare reconstruction.\n");
  fflush(stdout);
#endif
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(MPI_COMM_WORLD, &g_world_group);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  // g_world_group is a real id to avoid a lookup.
}

// AFAIK, we don't need to use the CVC algorithm (which is why we require g_world_comm to be defined) until reconstruction is complete.
void finalize_reconstruction() {
#ifdef DEBUG_VIDS
  printf("Finalize reconstruction.\n");
  fflush(stdout);
#endif

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_dup)(MPI_COMM_WORLD, &g_world_comm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  g_world_comm = ADD_NEW_COMM(g_world_comm);
}

// For all descriptors, set its real ID to the one uniquely described by its fields.
void reconstruct_with_descriptors() {
  prepare_reconstruction();
#ifdef DEBUG_VIDS
  printf("reconstruct_with_descriptors\n");
  print_id_descriptors();
  fflush(stdout);
#endif
  for (id_desc_pair pair : idDescriptorTable) {
    // Grab the first byte
    switch (pair.first & 0xFF000000) { 
      case UNDEFINED_MASK:
        break;
      case COMM_MASK:
#ifdef DEBUG_VIDS
	printf("recon_comm\n");
	fflush(stdout);
#endif
	reconstruct_with_comm_desc_t((comm_desc_t*)pair.second);
	break;
      case GROUP_MASK:
#ifdef DEBUG_VIDS
	printf("recon_group\n");
	fflush(stdout);
#endif
	reconstruct_with_group_desc_t((group_desc_t*)pair.second);
	break;
      case REQUEST_MASK:
	// update_request_desc_t((request_desc_t*)pair.second);
	break;
      case OP_MASK:
	// TODO does MPI_COMM_WORLD mask resolve to this?
#ifdef DEBUG_VIDS
	printf("recon_op\n");
	fflush(stdout);
#endif
	reconstruct_with_op_desc_t((op_desc_t*)pair.second);
	break;
      case DATATYPE_MASK:
#ifdef DEBUG_VIDS
	printf("recon_datatype\n");
	fflush(stdout);
#endif
	reconstruct_with_datatype_desc_t((datatype_desc_t*)pair.second);
	break;
      case FILE_MASK:
	// update_file_desc_t((file_desc_t*)pair.second);
	break;
      case COMM_KEYVAL_MASK:
	// update_comm_keyval_desc_t((comm_keyval_desc_t*)pair.second);
	break;
      default:
	break;
    }
  }
  finalize_reconstruction();
}
