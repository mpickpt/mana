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
#include <stdint.h>

#include "dmtcp.h"

#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "virtual-ids.h"
#include "lower_half_api.h"

#include "seq_num.h"
#include "mpi_nextfunc.h"

#define MAX_VIRTUAL_ID 999

MPI_Comm WORLD_COMM = MPI_COMM_WORLD;
MPI_Comm NULL_COMM = MPI_COMM_NULL;

// #define DEBUG_VIDS

// TODO I use an explicitly integer virtual id, which under macro
// reinterpretation will fit into an int64 pointer.  This should be
// fine if no real MPI Type is smaller than an int32.
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;

// Per Yao Xu, MANA does not require the thread safety offered by
// DMTCP's VirtualIdTable. We use std::map.

// int vId -> id_desc_t*, which contains rId.
std::map<int, id_desc_t*> idDescriptorTable; 

// int ggid -> ggid_desc_t*, which contains CVC information.
std::map<int, ggid_desc_t*> ggidDescriptorTable; 

// dead-simple vid generation mechanism.
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
  if (comm == NULL_COMM || comm == WORLD_COMM) {
    return (intptr_t)comm;
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

// We need to initialize the ggid_descriptor, because in order to
// checkpoint, we need to use the CVC algorithm to get out of the
// lower half everywhere.
// To do that, we need to use the LH to gather information.
// So, init_comm_desc is more expensive.
comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
  int worldRank, commSize, localRank;
#ifdef DEBUG_VIDS
    printf("init_comm_desc_t realComm: %x\n", realComm);
    fflush(stdout);
#endif

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_rank)(WORLD_COMM, &worldRank);
  NEXT_FUNC(Comm_size)(realComm, &commSize);
  NEXT_FUNC(Comm_rank)(realComm, &localRank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  int* ranks = ((int* )malloc(sizeof(int) * commSize));

  int ggid = getggid(realComm, worldRank, commSize, ranks);
  ggid_desc_iterator it = ggidDescriptorTable.find(ggid);
  comm_desc_t* desc = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));

  if (it != ggidDescriptorTable.end()) {
    // There exists another communicator with the same ggid, i.e.,
    // this communicator is an alias. We use the same ggid_desc.
    desc->ggid_desc = it->second;
  } else {
    ggid_desc_t* gd = ((ggid_desc_t *) malloc(sizeof(ggid_desc_t)));
    gd->ggid = ggid;
    // TODO What should the initial values be?
    // Either way, if the CVC algorithm sets its own initial values
    // later, these will be overwritten.
    gd->target_num = 0;
    gd->seq_num = 0;
    ggidDescriptorTable[ggid] = gd;
    desc->ggid_desc = gd;
  }

  free(ranks);
  desc->real_id = realComm;
  desc->local_rank = localRank;
  desc->ranks = NULL;
  return desc;
}

// HACK This function exists to call a communicator construction
// /without/ mutating the ggidDescriptorTable. This is ugly and
// probably should be changed later.
MPI_Comm get_vcomm_internal(MPI_Comm realComm) {
  int worldRank, commSize, localRank;
#ifdef DEBUG_VIDS
    printf("init_comm_desc_t realComm: %x\n", realComm);
    fflush(stdout);
#endif

  comm_desc_t* desc = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_rank)(MPI_COMM_WORLD, &worldRank);
  NEXT_FUNC(Comm_size)(realComm, &commSize);
  NEXT_FUNC(Comm_rank)(realComm, &localRank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();

  desc->real_id = realComm;
  desc->local_rank = localRank;
  desc->ranks = NULL;
  desc->size = commSize;

  // This should NOT be in the ggidDescTable.
  desc->ggid_desc = NULL;

  int vid = nextvId++;
  vid = vid | COMM_MASK;
  idDescriptorTable[vid] = ((union id_desc_t*) desc);

  MPI_Comm retval = *((MPI_Comm*)&vid);
  return retval;
}

// This is a communicator descriptor updater.
// Its job is to fill out the descriptor with all relevant metadata information (typically at precheckpoint time),
// which allows for O(1) reconstruction of the real id at restart time.

// TODO This may be inefficient because it causes repeated reconstruction of the respective group.
// A more efficient design would link groups and comms, but would introduce complexity.
void update_comm_desc_t(comm_desc_t* desc) {
  // HACK MPI_COMM_WORLD in the LH.
  // This is here because we need to virtualize MPI_COMM_WORLD for our own purposes.
  // But, since MPI_COMM_WORLD is a constant, it should not be reconstructed.
  if (desc->real_id == 0x84000000 || desc->real_id == MPI_COMM_WORLD) { 
    return;
  }

  // Cleanup from a previous CKPT, if one occured.
  free(desc->ranks);
  desc->ranks = NULL;


  MPI_Group group;

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(desc->real_id, &group);
  RETURN_TO_UPPER_HALF();
  // I do not free the resulting MPI_Group, because AFAIK, the
  // lower-half memory is thrown away on a restart anyways.

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

  free(local_ranks);
  desc->ranks = global_ranks;
}

// This is a communicator descriptor reconstructor.
// Its job is to take the metadata of the descriptor and re-create the communicator it describes.
// It is typically called at restart time.
void reconstruct_with_comm_desc_t(comm_desc_t* desc) {
  // HACK MPI_COMM_WORLD in the LH.
  // This is here because we need to virtualize MPI_COMM_WORLD for our own purposes.
  // But, since MPI_COMM_WORLD is a constant, it should not be reconstructed.
#ifdef DEBUG_VIDS
  printf("reconstruct_comm_desc_t comm: %x -> %x\n", desc->handle, desc->real_id);

  fflush(stdout);
  printf("reconstruct_with_comm_desc_t comm size: 0x%x\n", desc->size);

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

  MPI_Group group;
  
  // We recreate the communicator with the reconstructed group and MPI_COMM_WORLD.

  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->ranks, &group);
  NEXT_FUNC(Comm_create_group)(WORLD_COMM, group, 0, &desc->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}

// This is a communicator descriptor destructor.
// Its job is to free the communicator descriptor (but NOT the real communicator, or related, itself)
void destroy_comm_desc_t(comm_desc_t* desc) {
  free(desc->ranks);
  // If we destroy tons of communicators, this will leak memory.
  // A form of reference counting may be good but we need to ensure no race conditions.
  ggid_desc_t* tmp = desc->ggid_desc;
  // free(desc->ggid_desc);
  free(desc);
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  desc->ranks = NULL;
  desc->size = 0;
  /*
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

  free(ranks);
  desc->ranks = global_ranks;
  desc->size = groupSize;
  */

  return desc;
}

// Translate the local ranks of this group to global ranks, which are unique.
void update_group_desc_t(group_desc_t* group) {
  // TODO What happens when we call for a checkpoint while a checkpoint is already occurring? 
  DMTCP_PLUGIN_DISABLE_CKPT();

  // Cleanup from a previous checkpoint.
  free(group->ranks);
  group->ranks = NULL;

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

  free(local_ranks);
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

// TODO Currently not supported.  "The support of non-blocking
// collective commmunication is still not merged into the main branch.
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

// This update function is special, for the information we need is
// only available at creation time, and not with MPI calls.  We
// manually invoke this function at creation time. Mercifully, there
// is only one way to create a new operator, AFAIK.
void update_op_desc_t(op_desc_t* op, MPI_User_function* user_fn, int commute) {
  op->user_fn = user_fn;
  op->commute = commute;
}

void reconstruct_with_op_desc_t(op_desc_t* op) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Op_create)(op->user_fn, op->commute, &op->real_id);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
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

    // TODO this was in Yao's spec sheet, but I haven't seen it used
    // in any of the relevant functions. Why is this here?
    desc->num_large_counts = 0;
    desc->large_counts = NULL;

    desc->num_datatypes = 0;
    desc->datatypes = NULL;

    desc->combiner = 0;
    desc->is_freed = false;
    return desc;
}

void update_datatype_desc_t(datatype_desc_t* datatype) {
      // TODO this will fail for doubly-derived datatypes, for the
      // real id given in datatype->datatypes will not be constant
      // after a checkpoint-restart.
      if (datatype->is_freed) {
	// If the real datatype described has been freed in the lower half, MPI will be angry.
	return;
      }
      
      // Free the existing memory, if it is not NULL. (i.e., from an older checkpoint)
      free(datatype->integers);
      free(datatype->addresses);
      free(datatype->datatypes);
      datatype->integers = NULL;
      datatype->addresses = NULL;
      datatype->datatypes = NULL;

      bool should_return = false;

    DMTCP_PLUGIN_DISABLE_CKPT();

    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Type_get_envelope)(datatype->real_id, &datatype->num_integers, &datatype->num_addresses, &datatype->num_datatypes, &datatype->combiner);
    RETURN_TO_UPPER_HALF();

    // FIXME: "If combiner is MPI_COMBINER_NAMED then it is erroneous
    // to call MPI_TYPE_GET_CONTENTS. " So, we might want to exit
    // early (although this shouldn't happen anyway, as nobody should
    // make a virtualization of these types)

    // Use the malloc in the upper-half.
    datatype->integers = ((int*)malloc(sizeof(int) * datatype->num_integers));
    datatype->addresses = ((MPI_Aint*)malloc(sizeof(MPI_Aint) * datatype->num_addresses));
    datatype->datatypes = ((MPI_Datatype*)malloc(sizeof(MPI_Datatype) * datatype->num_datatypes));

    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Type_get_contents)(datatype->real_id, datatype->num_integers, datatype->num_addresses, datatype->num_datatypes, datatype->integers, datatype->addresses, datatype->datatypes);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
}

// FIXME: None of this will work with a datatype that is "doubly
// derived", i.e., with a datatype that is derived from another
// derived datatype.  We will correctly identify the combiner and get
// the envelope, however, the real ids we will obtain will be invalid.
// A correct implementation for doubly-derived datatypes would require
// something like a dependency tree for types.

// Mercifully, it appears that not many applications use
// doubly-derived datatypes. They do seem pretty baroque.

// Reconstruction arguments taken from here:
// https://www.mpi-forum.org/docs/mpi-3.1/mpi31-report/node90.htm
void reconstruct_with_datatype_desc_t(datatype_desc_t* datatype) {
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

// For internal purposes, we need to pre-emptively virtualize
// MPI_COMM_WORLD.
void init_comm_world() {
  comm_desc_t* comm_world = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));
  comm_world->real_id = WORLD_COMM;
  ggid_desc_t* comm_world_ggid = ((ggid_desc_t*)malloc(sizeof(ggid_desc_t)));
  comm_world->ggid_desc = comm_world_ggid;
  comm_world_ggid->ggid = (intptr_t)WORLD_COMM;
  comm_world_ggid->seq_num = 0;
  comm_world_ggid->target_num = 0;
  idDescriptorTable[(intptr_t)WORLD_COMM] = ((union id_desc_t*)comm_world);
  ggidDescriptorTable[MPI_COMM_WORLD] = comm_world_ggid;
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
  NEXT_FUNC(Comm_group)(WORLD_COMM, &g_world_group);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  // g_world_group is a real id to avoid a lookup. We're just using
  // the "set of ranks" property of groups here.
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
  NEXT_FUNC(Comm_dup)(WORLD_COMM, &g_world_comm);
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
}
