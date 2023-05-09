/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
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

// where key is the casted address of the virtual id_t, and value is the casted address of the real id_t.
// In the real id_t, real_comm, etc, are defined.
std::map<long, long> virt_to_real_map;

struct comm_id_t {
  MPI_Comm real_comm; // Real MPI communicator in the lower-half
  long handle; // A copy of the int type handle generated from the address of this struct
  unsigned int ggid; // Global Group ID
  unsigned long seq_num; // Sequence number for the CVC algorithm
  unsigned long target; // Target number for the CVC algorithm
  int size; // Size of this communicator
  int local_rank; // local rank number of this communicator
  int *ranks; // list of ranks of the group.
  // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?
}

struct group_id_t {
  MPI_Group real_group; // Real MPI group in the lower-half
  long handle; // A copy of the int type handle generated from the address of this struct
  int *ranks; // list of ranks of the group.
  // unsigned int ggid; // Global Group ID
}

struct request_id_t {
  MPI_Request request; // Real MPI request in the lower-half
  long handle; // A copy of the int type handle generated from the address of this struct
  enum request_kind; // P2P request or collective request
  MPI_Status status; // Real MPI status in the lower-half
}

struct op_id_t {
  MPI_Op real_op; // Real MPI operator in the lower-half
  long handle; // A copy of the int type handle generated from the address of this struct
  MPI_User_function *user_fn; // Function pointer to the user defined op function
}

struct datatype_id_t {
  MPI_Type real_datatype; // Real MPI type in the lower-half
  long handle; // A copy of the int type handle generated from the address of this struct
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
}

union id_t {
  comm_id_t comm;
  group_id_t group;
  request_id_t request;
  op_id_t op;
  datatype_id_t datatype;
}

function init_virt_comm_id_t(comm_id_t real_commid) {
}

function init_virt_group_id_t(group_id_t real_groupid) {
}

function init_virt_request_id_t(request_id_t real_requestid) {
}

function init_virt_op_id_t(op_id_t real_opid) {
}

function init_virt_datatype_id_t(datatype_id_t real_datatype) {
}

#define CONCAT(a, b) a ## b

#define VIRTUAL_TO_REAL(virt_objid) \
  *(__typeof__(&virt_objid))virt_to_real_map[virt_objid.handle]


#define REAL_TO_VIRTUAL(real_objid) \
  // TODO

// adds the given real id to the virtual id table
#define ADD_NEW(real_objid) \ 
  UPDATE_MAP(CONCAT(init_virt_,__typeof__(real_objid))(real_objid), real_objid)

#define REMOVE_OLD(virt_objid)
  virt_to_real_map.erase(virt_objid.handle)

// update an existing vid->rid mapping.
#define UPDATE_MAP(virt_objid, real_objid)
  virt_to_real_map[virt_objid.handle] = real_objid.handle
