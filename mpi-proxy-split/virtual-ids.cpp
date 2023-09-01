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

// Internal /real/ group, used to reconstruct and update. A way to access the
// whole bag of global ranks.
MPI_Group g_world_group;

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

    // FIXME: In the old system, using VirtualGlobalCommId, These were only
    // initiatialized in the wrapper function for Comm_create, (but not
    // Comm_split, etc.)
    // So, what is correct?
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

// FIXME: In some cases, this could cause a group to be needlessly constructed.
// A more efficient design would link groups and comms, but would introduce
// complexity.
void update_comm_desc_t(comm_desc_t* desc) {
  // We need to virtualize MPI_COMM_WORLD, but should not reconstruct it.
  if (desc->handle == MPI_COMM_WORLD) { 
    return;
  }

  // Cleanup from a previous CKPT, if one occured. FIXME: Is this the correct
  // way?
  free(desc->global_ranks);
  desc->global_ranks = NULL;

  MPI_Group group;

  // FIXME: Should we free this group?
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(desc->real_id, &group);
  RETURN_TO_UPPER_HALF();
  
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
  desc->global_ranks = global_ranks;
}

void reconstruct_with_comm_desc_t(comm_desc_t* desc) {
  if (desc->handle == MPI_COMM_WORLD) { 
    return;
  }
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
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->global_ranks, &group);
  NEXT_FUNC(Comm_create_group)(MPI_COMM_WORLD, group, 0, &desc->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void destroy_comm_desc_t(comm_desc_t* desc) {
  free(desc->global_ranks);
  // FIXME: We DO NOT free the ggid_desc, because it is aliased, and as such
  // shouldn't always be removed. We could do a form of reference counting, but
  // we would need to make sure that it is thread-safe. Need to think about how
  // to do it correctly in the MANA architecture.
  free(desc);
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  desc->global_ranks = NULL;
  desc->size = 0;
  return desc;
}

void update_group_desc_t(group_desc_t* group) {
  // Cleanup from a previous checkpoint.
  free(group->global_ranks);
  group->global_ranks = NULL;

  int groupSize;

  DMTCP_PLUGIN_DISABLE_CKPT();
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
  group->global_ranks = global_ranks;
  group->size = groupSize;
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void reconstruct_with_group_desc_t(group_desc_t* group) {
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, group->size, group->global_ranks, &group->real_id);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
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

// This update function is special, for the information we need is
// only available at creation time, and not with MPI calls.  We
// manually invoke this function at creation time. Mercifully, there
// is only one way to create a new operator, AFAIK.
void update_op_desc_t(op_desc_t* op, MPI_User_function* user_fn, int commute) {
  op->user_fn = user_fn;
  op->commute = commute;
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

    // FIXME: I had a design that would keep freed descriptors, to implement
    // doubly-derived reconstruction. But we can probably ignore this for now.
    
    // desc->is_freed = false;
    return desc;
}

void update_datatype_desc_t(datatype_desc_t* datatype) {
      // Free the existing memory, if it is not NULL. (i.e., from an older checkpoint)
    free(datatype->integers);
    free(datatype->addresses);
    free(datatype->datatypes);
    datatype->integers = NULL;
    datatype->addresses = NULL;
    datatype->datatypes = NULL;

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

// Reconstruction arguments taken from here:
// https://www.mpi-forum.org/docs/mpi-3.1/mpi31-report/node90.htm
void reconstruct_with_datatype_desc_t(datatype_desc_t* datatype) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    switch (datatype->combiner) {
            case MPI_COMBINER_DUP:
		    NEXT_FUNC(Type_dup)(datatype->datatypes[0], &datatype->real_id);
		    break;
            case MPI_COMBINER_NAMED:
	      // if the type is named and predefined, we shouldn't need to do anything.
	      // "If combiner is MPI_COMBINER_NAMED then it is erroneous to call MPI_TYPE_GET_CONTENTS. "
	            break;
	    case MPI_COMBINER_VECTOR:
		    NEXT_FUNC(Type_vector)(datatype->integers[0], datatype->integers[1], datatype->integers[2], datatype->datatypes[0], &datatype->real_id);
		    break;
	    case MPI_COMBINER_HVECTOR:
		    NEXT_FUNC(Type_hvector)(datatype->integers[0], datatype->integers[1], datatype->addresses[0], datatype->datatypes[0], &datatype->real_id);
		    break;
	    case MPI_COMBINER_INDEXED:
                    NEXT_FUNC(Type_indexed)(datatype->integers[0], datatype->integers + 1, datatype->integers + 1 + datatype->integers[0], datatype->datatypes[0], &datatype->real_id);
                    break;
	    case MPI_COMBINER_HINDEXED:
                    NEXT_FUNC(Type_hindexed)(datatype->integers[0], datatype->integers + 1, datatype->addresses, datatype->datatypes[0], &datatype->real_id);
                    break;
            case MPI_COMBINER_INDEXED_BLOCK:
	            NEXT_FUNC(Type_create_indexed_block)(datatype->integers[0], datatype->integers[1], datatype->integers + 2, datatype->datatypes[0], &datatype->real_id);
		    break;
            case MPI_COMBINER_HINDEXED_BLOCK:
	            NEXT_FUNC(Type_create_hindexed_block)(datatype->integers[0], datatype->integers[1], datatype->addresses, datatype->datatypes[0], &datatype->real_id);
		    break;
	    case MPI_COMBINER_STRUCT:
  		    NEXT_FUNC(Type_create_struct)(datatype->integers[0], datatype->integers + 1, datatype->addresses, datatype->datatypes, &datatype->real_id);
		    break;
            case MPI_COMBINER_SUBARRAY:
	      NEXT_FUNC(Type_create_subarray)(datatype->integers[0], datatype->integers + 1, datatype->integers + 1 + datatype->integers[0], datatype->integers + 1 + 2 * datatype->integers[0], datatype->integers[1 + 3 * datatype->integers[0]], datatype->datatypes[0], &datatype->real_id);
	            break;
            case MPI_COMBINER_DARRAY:
	      NEXT_FUNC(Type_create_darray)(datatype->integers[0], datatype->integers[1], datatype->integers[2], datatype->integers + 3, datatype->integers + 3 + datatype->integers[2], datatype->integers + 3 + 2 * datatype->integers[2], datatype->integers + 3 + 3 * datatype->integers[2], datatype->integers[3 + 4 * datatype->integers[2]], datatype->datatypes[0], &datatype->real_id);
            case MPI_COMBINER_CONTIGUOUS:
	            NEXT_FUNC(Type_contiguous)(datatype->integers[0], datatype->datatypes[0], &datatype->real_id);
		      break;
            case MPI_COMBINER_F90_REAL:
	      NEXT_FUNC(Type_create_f90_real)(datatype->integers[0], datatype->integers[1], &datatype->real_id);
	      break;
            case MPI_COMBINER_F90_COMPLEX:
	      NEXT_FUNC(Type_create_f90_complex)(datatype->integers[0], datatype->integers[1], &datatype->real_id);
	      break;
            case MPI_COMBINER_F90_INTEGER:
	      NEXT_FUNC(Type_create_f90_integer)(datatype->integers[0], &datatype->real_id);
	      break;
            case MPI_COMBINER_RESIZED:
	      NEXT_FUNC(Type_create_resized)(datatype->datatypes[0], datatype->addresses[0], datatype->addresses[1], &datatype->real_id);
	      break;
	  default:
		    break;
  }
  // FIXME: This is needed to make the reconstructed type usable in the
  // network. But there is a small potential for a double-commit bug here. Is
  // there an mpi function that checks if a type has been committed?
  //
  // Need to research if double-commit is an issue.
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
  comm_world->handle = MPI_COMM_WORLD;
  // FIXME: the other fields are not initialized. This is an INTERNAL
  // communicator descriptor, strictly for bookkeeping, not for reconstructing,
  // etc.

  idDescriptorTable[MPI_COMM_WORLD] = ((union id_desc_t*)comm_world);
  ggidDescriptorTable[MPI_COMM_WORLD] = comm_world_ggid;
}

// FIXME: This gets us a copy of the world group that we can use to get all the
// ranks. It's a real id, not a virtual one. Is this okay?
void write_g_world_group() {
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(MPI_COMM_WORLD, &g_world_group);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}

void destroy_g_world_group() {
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_free)(&g_world_group);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
}


// For all descriptors, update the respective information.
void update_descriptors() {
  write_g_world_group();
  for (id_desc_pair pair : idDescriptorTable) {
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
	// update_request_desc_t((request_desc_t*)pair.second);
	break;
      case OP_MASK:
	// update_op_desc_t((op_desc_t*)pair.second);
	// HACK: This must be called on Op_create. see update_op_desc
	break;
      case DATATYPE_MASK:
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
  destroy_g_world_group();
}

// For all descriptors, set its real ID to the one uniquely described by its fields.
void reconstruct_with_descriptors() {
  write_g_world_group();
  for (id_desc_pair pair : idDescriptorTable) {
    switch (pair.first & 0xFF000000) { 
      case UNDEFINED_MASK:
        break;
      case COMM_MASK:
	reconstruct_with_comm_desc_t((comm_desc_t*)pair.second);
	break;
      case GROUP_MASK:
	reconstruct_with_group_desc_t((group_desc_t*)pair.second);
	break;
      case REQUEST_MASK:
	// update_request_desc_t((request_desc_t*)pair.second);
	break;
      case OP_MASK:
	reconstruct_with_op_desc_t((op_desc_t*)pair.second);
	break;
      case DATATYPE_MASK:
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
  destroy_g_world_group();
}
