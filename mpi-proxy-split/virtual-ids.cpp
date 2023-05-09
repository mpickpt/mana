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

#include <mpi.h>

// where key is the casted address of the virtual id_t, and value is the casted address of the real id_t.
// In the real id_t, real_comm, etc, are defined.
std::map<long, long> virt_to_real_map;
std::map<long, long> virt_to_metadata_map;

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
}

struct virt_group_t {
  MPI_Group real_group; // Real MPI group in the lower-half
  int handle; // A copy of the int type handle generated from the address of this struct
  int *ranks; // list of ranks of the group.
  // unsigned int ggid; // Global Group ID
}

struct virt_request_t {
  MPI_Request request; // Real MPI request in the lower-half
  int handle; // A copy of the int type handle generated from the address of this struct
  enum request_kind; // P2P request or collective request
  MPI_Status status; // Real MPI status in the lower-half
}

struct virt_op_t {
  MPI_Op real_op; // Real MPI operator in the lower-half
  int handle; // A copy of the int type handle generated from the address of this struct
  MPI_User_function *user_fn; // Function pointer to the user defined op function
}

struct virt_datatype_t {
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
}

union virt_t {
  virt_comm_t comm;
  virt_group_t group;
  virt_request_t request;
  virt_op_t op;
  virt_datatype_t datatype;
}

#define CONCAT(a, b) a ## b

#define VIRTUAL_TO_REAL(virt_objid) \
  *(__typeof__(&virt_objid))virt_to_real_map[*(long *)virt_objid]

//#define REAL_TO_VIRTUAL(real_objid)		\

// adds the given real id to the virtual id table, returns virtual id
#define ADD_NEW(real_objid) \ 
{
  // TODO Per-type logic: Create metadata
}

// Removes an old vid mapping, returns the mapped real id. Assumes the mapping exists presently.
#define REMOVE_OLD(virt_objid) \
  {
  long it = virt_to_real_map.find(*(long *)virt_objid);
__typeof__(&virt_objid) real_objid = *(__typeof__(&virt_objid))it;
virt_to_real_map.erase(*(long *)virt_objid);
  real_objid;
  }

// update an existing vid->rid mapping. 
#define UPDATE_MAP(virt_objid, real_objid) \
  virt_to_real_map[*(long *)virt_objid] = *(long *)real_objid
