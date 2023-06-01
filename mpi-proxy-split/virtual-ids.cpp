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

struct ggid_t;

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable.
std::map<int, void *> vTypeTable; // int vId -> void* virt_t vType. vType contains rId. Designed for MPICH for now (i.e. uses ints).
std::map<int, ggid_t*> ggidTable; // int ggid -> ggid_t*
typedef typename std::map<int, void*>::iterator id_iterator;
typedef typename std::map<int, ggid_t*>::iterator ggid_iterator; // Yes, these are aliases.

int base = 1;
int nextvId = base;

// Writing these macros as ternary expressions means there is no overhead associated with extra function arguments.

// These casts mean that the user only sees the real id from vType.
// The REAL IDs are contained in heap-allocated vType structures. So we cast again.
#define REAL_TO_VIRTUAL(id, null) \ 
  (id == null) ? null : *(__typeof__(&id))realToVirtual((void*)&id)

#define VIRTUAL_TO_REAL(id, null) \
  (id == null) ? null : *(__typeof__(&id))virtualToReal(id)

#define ADD_NEW(id, null, virtual_type)						\
  (id == null) ? null : *(__typeof__(&id))onCreate(id, malloc(sizeof(virtual_type)))

#define REMOVE_OLD(id, null) \
  (id == null) ? null : *(__typeof__(&id))onRemove(id)

#define UPDATE_MAP(v, r, null) \
  (id == null) ? null : *(__typeof__(&id))updateMapping(v, r)/

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
  (id == MPI_COMM_NULL) ? null : *(__typeof__(&id))onCreateComm(id, malloc(sizeof(virt_comm_t))) // HACK.
  // ADD_NEW(id, MPI_COMM_NULL, virt_comm_t)
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


struct ggid_t {
  int ggid; // hashing results of communicator members

  unsigned long seq_num;

  unsigned long target_num;
};

struct virt_comm_t {
    MPI_Comm real_id; // Real MPI communicator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    ggid_t* ggid; // A ggid_t structure, containing CVC information for this communicator.
    int size; // Size of this communicator
    int local_rank; // local rank number of this communicator
    int *ranks; // list of ranks of the group.

    // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?

    // These fields are obsoleted by ggid_t.
    // unsigned int ggid; // Global Group ID
    // unsigned long seq_num; // Sequence number for the CVC algorithm
    // unsigned long target; // Target number for the CVC algorithm
};

struct virt_group_t {
    MPI_Group real_id; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
};

struct virt_request_t {
    MPI_Request real_id; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    enum request_kind; // P2P request or collective request
    MPI_Status status; // Real MPI status in the lower-half
};

struct virt_op_t {
    MPI_Op real_id; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
};

struct virt_datatype_t {
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

union virt_t {
    virt_comm_t comm;
    virt_group_t group;
    virt_request_t request;
    virt_op_t op;
    virt_datatype_t datatype;
};

int realToVirtual(void* realId) {
  return ((virt_comm_t*)realId)->handle;
}

int virtualToReal(int virtId) {
  id_iterator it = vTypeTable.find(virtId);
  if (it != vTypeTable.end()) {
    return ((virt_comm_t *)it->second)->real_id;
  }
  return NULL;
}

int updateMapping(int virtId, int realId) { // HACK MPICH
  void* vType = vTypeTable[virtId];
  ((virt_comm_t*)vType)->real_id = realId;
  return realId;
}

int onCreate(int realId, void* vType) { // HACK MPICH
  int vId = nextvId++;
  ((virt_comm_t*)vType)->handle = *(long *)vType;
  ((virt_comm_t*)vType)->real_id = realId;
  ((virt_comm_t*)vType)->handle = vId;

  vTypeTable[vId] = vType;
  return realId;
}

int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

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

int onCreateComm(int realId, void* vType) {
  int ggid = getggid(realId);
  ggid_iterator it = ggidTable.find(ggid);

  if (it != ggidTable.end()) {
    ((virt_comm_t*) vType)->ggid = ((ggid_t *)it->second);
  } else {
    ggid_t* g = ((ggid_t *) malloc(sizeof(ggid_t)));
    g->ggid = ggid;
    ggidTable[ggid] = g;
    ((virt_comm_t*) vType)->ggid = g;
  }
}

int onRemove(int virtId) {
  void* vType;

  id_iterator it = vTypeTable.find(virtId);
  if (it != vTypeTable.end()) {
    vType = it->second;
  }
  vTypeTable.erase(virtId);

  int realId = ((virt_comm_t *)vType)->real_id; // HACK MPICH
  free(vType);
  return realId;
}

