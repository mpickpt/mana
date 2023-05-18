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

#define MAX_VIRTUAL_ID 999

// Per Yao Xu, MANA does not require the thread safety offered by DMTCP's VirtualIdTable.
std::map<long, long> vIdTable;
typedef typename std::map<long, long>::iterator id_iterator;

long realToVirtual(long realId) {
  for (id_iterator i =  vIdTable.begin(); i != vIdTable.end(); ++i) {
    if (realId == i->second) {
      return i->first;
    }
  }
  return realId;
}

long virtualToReal(long virtId) {
  return vIdTable[virtId];
}

long updateMapping(long virtId, long realId) {
  vIdTable[virtId] = realId;
  return virtId;
}

long onCreate(long realId, void* metadata) {
  // TODO fill out handle
  return updateMapping(*(long *)metadata, realId);
}

long onRemove(long virtId) {
  // TODO if exists
  long realId = virtualToReal(virtId);
  vIdTable.erase(virtId);
  return realId;
}

// Writing these macros as ternary expressions means there is no overhead associated with extra function arguments.

#define REAL_TO_VIRTUAL_FILE(id) \
  ((*(long* ) id) == MPI_FILE_NULL) ? MPI_FILE_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_FILE(id) \
  ((*(long* ) id) == MPI_FILE_NULL) ? MPI_FILE_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_FILE(id) \
  ((*(long* ) id) == MPI_FILE_NULL) ? MPI_FILE_NULL : onCreate(*(long *)id, malloc(sizeof(virt_file_t)))
#define REMOVE_OLD_FILE(id) \
  ((*(long* ) id) == MPI_FILE_NULL) ? MPI_FILE_NULL : onRemove(*(long *)id)
#define UPDATE_FILE_MAP(v, r) \
  ((*(long* ) id) == MPI_FILE_NULL) ? MPI_FILE_NULL : updateMapping(*(long *)v, *(long* ) r)/


#define REAL_TO_VIRTUAL_COMM(id) \
  ((*(long* ) id) == MPI_COMM_NULL) ? MPI_COMM_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_COMM(id) \
  ((*(long* ) id) == MPI_COMM_NULL) ? MPI_COMM_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_COMM(id) \
  ((*(long* ) id) == MPI_COMM_NULL) ? MPI_COMM_NULL : onCreate(*(long *)id, malloc(sizeof(virt_comm_t)))
#define REMOVE_OLD_COMM(id) \
  ((*(long* ) id) == MPI_COMM_NULL) ? MPI_COMM_NULL : onRemove(*(long *)id)
#define UPDATE_COMM_MAP(v, r) \
  ((*(long* ) id) == MPI_COMM_NULL) ? MPI_COMM_NULL : updateMapping(*(long *)v, *(long* ) r)

#define REAL_TO_VIRTUAL_GROUP(id) \
  ((*(long* ) id) == MPI_GROUP_NULL) ? MPI_GROUP_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_GROUP(id) \
  ((*(long* ) id) == MPI_GROUP_NULL) ? MPI_GROUP_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_GROUP(id) \
  ((*(long* ) id) == MPI_GROUP_NULL) ? MPI_GROUP_NULL : onCreate(*(long *)id, malloc(sizeof(virt_group_t)))
#define REMOVE_OLD_GROUP(id) \
  ((*(long* ) id) == MPI_GROUP_NULL) ? MPI_GROUP_NULL : onRemove(*(long *)id)
#define UPDATE_GROUP_MAP(v, r) \
  ((*(long* ) id) == MPI_GROUP_NULL) ? MPI_GROUP_NULL : updateMapping(*(long *)v, *(long* ) r)


#define REAL_TO_VIRTUAL_TYPE(id) \
  ((*(long* ) id) == MPI_TYPE_NULL) ? MPI_TYPE_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_TYPE(id) \
  ((*(long* ) id) == MPI_TYPE_NULL) ? MPI_TYPE_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_TYPE(id) \
  ((*(long* ) id) == MPI_TYPE_NULL) ? MPI_TYPE_NULL : onCreate(*(long *)id, malloc(sizeof(virt_type_t)))
#define REMOVE_OLD_TYPE(id) \
  ((*(long* ) id) == MPI_TYPE_NULL) ? MPI_TYPE_NULL : onRemove(*(long *)id)
#define UPDATE_TYPE_MAP(v, r) \
  ((*(long* ) id) == MPI_TYPE_NULL) ? MPI_TYPE_NULL : updateMapping(*(long *)v, *(long* ) r)


#define REAL_TO_VIRTUAL_OP(id) \
  ((*(long* ) id) == MPI_OP_NULL) ? MPI_OP_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_OP(id) \
  ((*(long* ) id) == MPI_OP_NULL) ? MPI_OP_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_OP(id) \
  ((*(long* ) id) == MPI_OP_NULL) ? MPI_OP_NULL : onCreate(*(long *)id, malloc(sizeof(virt_op_t)))
#define REMOVE_OLD_OP(id) \
  ((*(long* ) id) == MPI_OP_NULL) ? MPI_OP_NULL : onRemove(*(long *)id)
#define UPDATE_OP_MAP(v, r) \
  ((*(long* ) id) == MPI_OP_NULL) ? MPI_OP_NULL : updateMapping(*(long *)v, *(long* ) r)

#define REAL_TO_VIRTUAL_COMM_KEYVAL(id) \
  ((*(long* ) id) == MPI_COMM_KEYVAL_NULL) ? MPI_COMM_KEYVAL_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  ((*(long* ) id) == MPI_COMM_KEYVAL_NULL) ? MPI_COMM_KEYVAL_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_COMM_KEYVAL(id) \
  ((*(long* ) id) == MPI_COMM_KEYVAL_NULL) ? MPI_COMM_KEYVAL_NULL : onCreate(*(long *)id, malloc(sizeof(virt_comm_keyval_t)))
#define REMOVE_OLD_COMM_KEYVAL(id) \
  ((*(long* ) id) == MPI_COMM_KEYVAL_NULL) ? MPI_COMM_KEYVAL_NULL : onRemove(*(long *)id)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  ((*(long* ) id) == MPI_COMM_KEYVAL_NULL) ? MPI_COMM_KEYVAL_NULL : updateMapping(*(long *)v, *(long* ) r)

#define REAL_TO_VIRTUAL_REQUEST(id) \
  ((*(long* ) id) == MPI_REQUEST_NULL) ? MPI_REQUEST_NULL : realToVirtual(*(long *)id)
#define VIRTUAL_TO_REAL_REQUEST(id) \
  ((*(long* ) id) == MPI_REQUEST_NULL) ? MPI_REQUEST_NULL : virtualToReal(*(long *)id)
#define ADD_NEW_REQUEST(id) \
  ((*(long* ) id) == MPI_REQUEST_NULL) ? MPI_REQUEST_NULL : onCreate(*(long *)id, malloc(sizeof(virt_request_t)))
#define REMOVE_OLD_REQUEST(id) \
  ((*(long* ) id) == MPI_REQUEST_NULL) ? MPI_REQUEST_NULL : onRemove(*(long *)id)
#define UPDATE_REQUEST_MAP(v, r) \
  ((*(long* ) id) == MPI_REQUEST_NULL) ? MPI_REQUEST_NULL : updateMapping(*(long *)v, *(long* ) r)

// --- metadata structs ---

struct virt_comm_t {
    MPI_Comm real_comm; // Real MPI communicator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    unsigned int ggid; // Global Group ID
    unsigned long seq_num; // Sequence number for the CVC algorithm
    unsigned long target; // Target number for the CVC algorithm
    int size; // Size of this communicator
    int local_rank; // local rank number of this communicator
    int *ranks; // list of ranks of the group.
    // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?
};

struct virt_group_t {
    MPI_Group real_group; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
};

struct virt_request_t {
    MPI_Request request; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    enum request_kind; // P2P request or collective request
    MPI_Status status; // Real MPI status in the lower-half
};

struct virt_op_t {
    MPI_Op real_op; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
};

struct virt_datatype_t {
  // TODO add mpi type identifier field virtual class
    MPI_Type real_datatype; // Real MPI type in the lower-half
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
