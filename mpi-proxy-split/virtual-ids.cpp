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

#define MAX_VIRTUAL_ID 999

// perlmutter -- load more modules

// load cray-pmi (process management i-)
// unload cudatoolkit
// unload gpu

struct ggid_desc_t;
union id_desc_t;

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable. We use std::map.
std::map<int, id_desc_t*> idDescriptorTable; // int vId -> id_desc_t*, which contains rId.
std::map<int, ggid_desc_t*> ggidDescriptorTable; // int ggid -> ggid_desc_t*
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;

int base = 1;
int nextvId = base;

// Writing these macros as ternary expressions means there is no overhead associated with extra function arguments.
#define DESCRIPTOR_TO_VIRTUAL(id, null) \ 
  (id == null) ? null : descriptorToVirtual(type)

#define VIRTUAL_TO_DESCRIPTOR(id, null) \
  (id == null) ? null : virtualToDescriptor(id)

#define ADD_NEW(real_id, null, descriptor_type)						\
  (real_id == null) ? null : onCreate(real_id, malloc(sizeof(descriptor_type)))

#define REMOVE_OLD(virtual_id, null) \
  (virtual_id == null) ? null : onRemove(virtual_id)

#define UPDATE_MAP(virtual_id, real_id, null) \
  (virtual_id == null) ? null : updateMapping(virtual_id, real_id)/

#define REAL_TO_VIRTUAL_FILE(id) \
  REAL_TO_VIRTUAL(id, MPI_FILE_NULL) 
#define VIRTUAL_TO_REAL_FILE(id) \
  VIRTUAL_TO_REAL(id, MPI_FILE_NULL)
#define ADD_NEW_FILE(id) \
  ADD_NEW(id, MPI_FILE_NULL, virt_file_t)
#define REMOVE_OLD_FILE(id) \
  REMOVE_OLD(id, MPI_FILE_NULL)
#define UPDATE_FILE_MAP(v, r) \
  UPDATE_MAP(id, MPI_FILE_NULL)

#define REAL_TO_VIRTUAL_COMM(id) \
  REAL_TO_VIRTUAL(id, MPI_COMM_NULL) 
#define VIRTUAL_TO_REAL_COMM(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_NULL)
#define ADD_NEW_COMM(id) \
  (id == MPI_COMM_NULL) ? null : onCreateComm(id, malloc(sizeof(comm_desc_t))) // HACK. Communicators are special, we need to give a ggid.
  // ADD_NEW(id, MPI_COMM_NULL, comm_desc_t)
#define REMOVE_OLD_COMM(id) \
  REMOVE_OLD(id, MPI_COMM_NULL)
#define UPDATE_COMM_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_NULL)

#define REAL_TO_VIRTUAL_GROUP(id) \
  REAL_TO_VIRTUAL(id, MPI_GROUP_NULL) 
#define VIRTUAL_TO_REAL_GROUP(id) \
  VIRTUAL_TO_REAL(id, MPI_GROUP_NULL)
#define ADD_NEW_GROUP(id) \
  ADD_NEW(id, MPI_GROUP_NULL, virt_group_t)
#define REMOVE_OLD_GROUP(id) \
  REMOVE_OLD(id, MPI_GROUP_NULL)
#define UPDATE_GROUP_MAP(v, r) \
  UPDATE_MAP(id, MPI_GROUP_NULL)

#define REAL_TO_VIRTUAL_TYPE(id) \
  REAL_TO_VIRTUAL(id, MPI_TYPE_NULL) 
#define VIRTUAL_TO_REAL_TYPE(id) \
  VIRTUAL_TO_REAL(id, MPI_TYPE_NULL)
#define ADD_NEW_TYPE(id) \
  ADD_NEW(id, MPI_TYPE_NULL, virt_type_t)
#define REMOVE_OLD_TYPE(id) \
  REMOVE_OLD(id, MPI_TYPE_NULL)
#define UPDATE_TYPE_MAP(v, r) \
  UPDATE_MAP(id, MPI_TYPE_NULL)

#define REAL_TO_VIRTUAL_OP(id) \
  REAL_TO_VIRTUAL(id, MPI_OP_NULL) 
#define VIRTUAL_TO_REAL_OP(id) \
  VIRTUAL_TO_REAL(id, MPI_OP_NULL)
#define ADD_NEW_OP(id) \
  ADD_NEW(id, MPI_OP_NULL, virt_op_t)
#define REMOVE_OLD_OP(id) \
  REMOVE_OLD(id, MPI_OP_NULL)
#define UPDATE_OP_MAP(v, r) \
  UPDATE_MAP(id, MPI_OP_NULL)

#define REAL_TO_VIRTUAL_COMM_KEYVAL(id) \
  REAL_TO_VIRTUAL(id, MPI_COMM_KEYVAL_NULL) 
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_KEYVAL_NULL)
#define ADD_NEW_COMM_KEYVAL(id) \
  ADD_NEW(id, MPI_COMM_KEYVAL_NULL, virt_comm_keyval_t)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  REMOVE_OLD(id, MPI_COMM_KEYVAL_NULL)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_KEYVAL_NULL)

#define REAL_TO_VIRTUAL_REQUEST(id) \
  REAL_TO_VIRTUAL(id, MPI_REQUEST_NULL) 
#define VIRTUAL_TO_REAL_REQUEST(id) \
  VIRTUAL_TO_REAL(id, MPI_REQUEST_NULL)
#define ADD_NEW_REQUEST(id) \
  ADD_NEW(id, MPI_REQUEST_NULL, virt_request_t)
#define REMOVE_OLD_REQUEST(id) \
  REMOVE_OLD(id, MPI_REQUEST_NULL)
#define UPDATE_REQUEST_MAP(v, r) \
  UPDATE_MAP(id, MPI_REQUEST_NULL)

#ifndef NEXT_FUNC
# define NEXT_FUNC(func)                                                       \
  ({                                                                           \
    static __typeof__(&MPI_##func)_real_MPI_## func =                          \
                                                (__typeof__(&MPI_##func)) - 1; \
    if (_real_MPI_ ## func == (__typeof__(&MPI_##func)) - 1) {                 \
      _real_MPI_ ## func = (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);    \
    }                                                                          \
    _real_MPI_ ## func;                                                        \
  })
#endif // ifndef NEXT_FUNC

// --- metadata structs ---


struct ggid_desc_t {
  int ggid; // hashing results of communicator members

  unsigned long seq_num;

  unsigned long target_num;
};

struct comm_desc_t {
    MPI_Comm real_id; // Real MPI communicator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    ggid_desc_t* ggid_desc; // A ggid_t structure, containing CVC information for this communicator.
    int size; // Size of this communicator
    int local_rank; // local rank number of this communicator
    int *ranks; // list of ranks of the group.

    // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?

    // These fields are obsoleted by ggid_t.
    // unsigned int ggid; // Global Group ID
    // unsigned long seq_num; // Sequence number for the CVC algorithm
    // unsigned long target; // Target number for the CVC algorithm
};

struct group_desc_t {
    MPI_Group real_id; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
};

struct request_desc_t {
    MPI_Request real_id; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    enum request_kind; // P2P request or collective request
    MPI_Status status; // Real MPI status in the lower-half
};

struct op_desc_t {
    MPI_Op real_id; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
};

struct datatype_desc_t {
  // TODO add mpi type identifier field virtual class
    MPI_Type real_id; // Real MPI type in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    // Components of user-defined datatype.
    MPI_count num_integers;
    int *integers;
    MPI_count num_addresses;
    int *addresses;
    MPI_count num_large_counts;
    int *large_counts;
    MPI_count num_datatypes;
    int *datatypes;
    int *combiner;
};

union id_desc_t {
    comm_desc_t comm;
    group_desc_t group;
    request_desc_t request;
    op_desc_t op;
    datatype_desc_t datatype;
};

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
id_desc_t* updateMapping(int virtId, __typeof__(MPI_Comm) realId) { // HACK MPICH
  id_desc_iterator it = idDescriptorTable.find(virtId);
  if (it != idDescriptorTable.end()) {
    id_desc_t* desc = idDescriptorTable[virtId]; // if doesn't exist?
    ((comm_desc_t*)desc)->real_id = realId;
    return desc;
  } else {
    // TODO
    return NULL;
  }
}

// Given realId of MPI size and id descriptor, fill out the id descriptor and save it in the table.
// Returns the vId assigned.
int onCreate(__typeof__(MPI_Comm) realId, id_desc_t* desc) { // HACK MPICH
  int vId = nextvId++; // TODO
  ((comm_desc_t*)desc)->real_id = realId;
  ((comm_desc_t*)desc)->handle = vId;

  idDescriptorTable[vId] = desc;
  return vId;
}

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

// Given a real MPI Communicator and id descriptor, fill out the descriptor with the ggid.
// Then, proceed as with any other real id.
// Returns the vId assigned.
int onCreateComm(MPI_Comm realComm, comm_desc_t* desc) {
  int vId = nextvId++;
  int ggid = getggid(realId);
  ggid_iterator it = ggidTable.find(ggid);

  if (it != ggidTable.end()) {
    desc->ggid_desc = it->second;
  } else {
    ggid_desc_t* gd = ((ggid_desc_t *) malloc(sizeof(ggid_desc_t)));
    gd->ggid = ggid;
    ggidTable[ggid] = gd;
    desc->ggid_desc = gd;
  }
  return onCreate(realComm, vType);
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

