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

// Per Yao Xu, MANA does not require the thread safety offered by
// DMTCP's VirtualIdTable. We use std::map.

// int vId -> id_desc_t*, which contains rId.
std::map<int, id_desc_t*> idDescriptorTable; 

// dead-simple vid generation mechanism.
int base = 1;
int nextvId = base;

// This is a descriptor initializer. Its job is to write an initial
// descriptor for a real MPI Communicator.

comm_desc_t* init_comm_desc_t(MPI_Comm realComm) {
  comm_desc_t* desc = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));
  desc->real_id = realComm;
  desc->ranks = NULL;
  return desc;
}

void destroy_comm_desc_t(comm_desc_t* desc) {
  free(desc->ranks);
  free(desc);
}

group_desc_t* init_group_desc_t(MPI_Group realGroup) {
  group_desc_t* desc = ((group_desc_t*)malloc(sizeof(group_desc_t)));
  desc->real_id = realGroup;
  desc->ranks = NULL;
  desc->size = 0;
  return desc;
}

void destroy_group_desc_t(group_desc_t* group) {
  free(group->ranks);
  free(group);
}

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

// We need to preemptively virtualize MPI_COMM_WORLD.
void init_comm_world() {
  comm_desc_t* comm_world = ((comm_desc_t*)malloc(sizeof(comm_desc_t)));

  // FIXME: This WILL NOT WORK when moving to OpenMPI, ExaMPI, as written.
  // Yao, Twinkle, should apply their lh_constants_map strategy here.
  comm_world->real_id = 0x84000000;

  idDescriptorTable[MPI_COMM_WORLD] = ((union id_desc_t*)comm_world);
}
