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

#ifndef _LOWER_HALF_API_H
#define _LOWER_HALF_API_H

#include <mpi.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>

#include "switch_context.h"

extern "C" int MPI_MANA_Internal(char *dummy);

typedef char* VA;

typedef struct __MmapInfo
{
  void *addr;
  size_t len;
} MmapInfo_t;

// The transient lh_proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
// This must be same in restart_plugin and low
typedef struct _LowerHalfInfo
{
  void *fsaddr; // The base value of the FS register of the lower half
#ifdef SINGLE_CART_REORDER
  void *getCoordinatesFptr; // Pointer to getCoordinates() function in the lower half
  void *getCartesianCommunicatorFptr; // Pointer to getCartesianCommunicator() function in the lower half
#endif
  void *getMmappedListFptr; // Pointer to getMmappedList() function in the lower half
  void *resetMmappedListFptr; // Pointer to resetMmappedList() function in the lower half
  void *mmap;
  void *munmap;
  void *mmap_list_fptr;
  void *lh_dlsym;
  void *uh_stack;

  // MPI Constants
  MPI_Group MANA_GROUP_NULL;
  MPI_Comm MANA_COMM_NULL;
  MPI_Request MANA_REQUEST_NULL;
  MPI_Message MANA_MESSAGE_NULL;
  MPI_Op MANA_OP_NULL;
  MPI_Errhandler MANA_ERRHANDLER_NULL;
  MPI_Info MANA_INFO_NULL;
  MPI_Win MANA_WIN_NULL;
  MPI_File MANA_FILE_NULL;
  MPI_Info MANA_INFO_ENV;
  MPI_Comm MANA_COMM_WORLD;
  MPI_Comm MANA_COMM_SELF;
  MPI_Group MANA_GROUP_EMPTY;
  MPI_Message MANA_MESSAGE_NO_PROC;
  MPI_Op MANA_MAX;
  MPI_Op MANA_MIN;
  MPI_Op MANA_SUM;
  MPI_Op MANA_PROD;
  MPI_Op MANA_LAND;
  MPI_Op MANA_BAND;
  MPI_Op MANA_LOR;
  MPI_Op MANA_BOR;
  MPI_Op MANA_LXOR;
  MPI_Op MANA_BXOR;
  MPI_Op MANA_MAXLOC;
  MPI_Op MANA_MINLOC;
  MPI_Op MANA_REPLACE;
  MPI_Op MANA_NO_OP;
  MPI_Datatype MANA_DATATYPE_NULL;
  MPI_Datatype MANA_BYTE;
  MPI_Datatype MANA_PACKED;
  MPI_Datatype MANA_CHAR;
  MPI_Datatype MANA_SHORT;
  MPI_Datatype MANA_INT;
  MPI_Datatype MANA_LONG;
  MPI_Datatype MANA_FLOAT;
  MPI_Datatype MANA_DOUBLE;
  MPI_Datatype MANA_LONG_DOUBLE;
  MPI_Datatype MANA_UNSIGNED_CHAR;
  MPI_Datatype MANA_SIGNED_CHAR;
  MPI_Datatype MANA_UNSIGNED_SHORT;
  MPI_Datatype MANA_UNSIGNED_LONG;
  MPI_Datatype MANA_UNSIGNED;
  MPI_Datatype MANA_FLOAT_INT;
  MPI_Datatype MANA_DOUBLE_INT;
  MPI_Datatype MANA_LONG_DOUBLE_INT;
  MPI_Datatype MANA_LONG_INT;
  MPI_Datatype MANA_SHORT_INT;
  MPI_Datatype MANA_2INT;
  MPI_Datatype MANA_WCHAR;
  MPI_Datatype MANA_LONG_LONG_INT;
  MPI_Datatype MANA_LONG_LONG;
  MPI_Datatype MANA_UNSIGNED_LONG_LONG;
#if 0
  MPI_Datatype MANA_2COMPLEX;
  MPI_Datatype MANA_2DOUBLE_COMPLEX;
  MPI_Datatype MANA_CXX_COMPLEX;
#endif
  MPI_Datatype MANA_CHARACTER;
  MPI_Datatype MANA_LOGICAL;
#if 0
  MPI_Datatype MANA_LOGICAL1;
  MPI_Datatype MANA_LOGICAL2;
  MPI_Datatype MANA_LOGICAL4;
  MPI_Datatype MANA_LOGICAL8;
#endif
  MPI_Datatype MANA_INTEGER;
  MPI_Datatype MANA_INTEGER1;
  MPI_Datatype MANA_INTEGER2;
  MPI_Datatype MANA_INTEGER4;
  MPI_Datatype MANA_INTEGER8;
  MPI_Datatype MANA_REAL;
  MPI_Datatype MANA_REAL4;
  MPI_Datatype MANA_REAL8;
  MPI_Datatype MANA_REAL16;
  MPI_Datatype MANA_DOUBLE_PRECISION;
  MPI_Datatype MANA_COMPLEX;
  MPI_Datatype MANA_COMPLEX8;
  MPI_Datatype MANA_COMPLEX16;
  MPI_Datatype MANA_COMPLEX32;
  MPI_Datatype MANA_DOUBLE_COMPLEX;
  MPI_Datatype MANA_2REAL;
  MPI_Datatype MANA_2DOUBLE_PRECISION;
  MPI_Datatype MANA_2INTEGER;
  MPI_Datatype MANA_INT8_T;
  MPI_Datatype MANA_UINT8_T;
  MPI_Datatype MANA_INT16_T;
  MPI_Datatype MANA_UINT16_T;
  MPI_Datatype MANA_INT32_T;
  MPI_Datatype MANA_UINT32_T;
  MPI_Datatype MANA_INT64_T;
  MPI_Datatype MANA_UINT64_T;
  MPI_Datatype MANA_AINT;
  MPI_Datatype MANA_OFFSET;
  MPI_Datatype MANA_C_BOOL;
  MPI_Datatype MANA_C_COMPLEX;
  MPI_Datatype MANA_C_FLOAT_COMPLEX;
  MPI_Datatype MANA_C_DOUBLE_COMPLEX;
  MPI_Datatype MANA_C_LONG_DOUBLE_COMPLEX;
  MPI_Datatype MANA_CXX_BOOL;
  MPI_Datatype MANA_CXX_FLOAT_COMPLEX;
  MPI_Datatype MANA_CXX_DOUBLE_COMPLEX;
  MPI_Datatype MANA_CXX_LONG_DOUBLE_COMPLEX;
  MPI_Datatype MANA_COUNT;
  MPI_Errhandler MANA_ERRORS_ARE_FATAL;
  MPI_Errhandler MANA_ERRORS_RETURN;
} LowerHalfInfo_t;

extern LowerHalfInfo_t *lh_info;  

#define FOREACH_FNC(MACRO) \
  MACRO(Init) \
  MACRO(Finalize) \
  MACRO(Send) \
  MACRO(Recv) \
  MACRO(Type_size) \
  MACRO(Iprobe) \
  MACRO(Get_count) \
  MACRO(Isend) \
  MACRO(Irecv) \
  MACRO(Wait) \
  MACRO(Test) \
  MACRO(Bcast) \
  MACRO(Abort) \
  MACRO(Barrier) \
  MACRO(Reduce) \
  MACRO(Allreduce) \
  MACRO(Alltoall) \
  MACRO(Alltoallv) \
  MACRO(Comm_split) \
  MACRO(Accumulate) \
  MACRO(Add_error_class) \
  MACRO(Add_error_code) \
  MACRO(Add_error_string) \
  MACRO(Address) \
  MACRO(Allgather) \
  MACRO(Iallgather) \
  MACRO(Allgatherv) \
  MACRO(Iallgatherv) \
  MACRO(Alloc_mem) \
  MACRO(Iallreduce) \
  MACRO(Ialltoall) \
  MACRO(Ialltoallv) \
  MACRO(Alltoallw) \
  MACRO(Ialltoallw) \
  MACRO(Attr_delete) \
  MACRO(Attr_get) \
  MACRO(Attr_put) \
  MACRO(Ibarrier) \
  MACRO(Bsend) \
  MACRO(Ibcast) \
  MACRO(Bsend_init) \
  MACRO(Buffer_attach) \
  MACRO(Buffer_detach) \
  MACRO(Cancel) \
  MACRO(Cart_coords) \
  MACRO(Cart_create) \
  MACRO(Cart_get) \
  MACRO(Cart_map) \
  MACRO(Cart_rank) \
  MACRO(Cart_shift) \
  MACRO(Cart_sub) \
  MACRO(Cartdim_get) \
  MACRO(Close_port) \
  MACRO(Comm_accept) \
  MACRO(Comm_call_errhandler) \
  MACRO(Comm_compare) \
  MACRO(Comm_connect) \
  MACRO(Comm_create_errhandler) \
  MACRO(Comm_create_keyval) \
  MACRO(Comm_create_group) \
  MACRO(Comm_create) \
  MACRO(Comm_delete_attr) \
  MACRO(Comm_disconnect) \
  MACRO(Comm_dup) \
  MACRO(Comm_idup) \
  MACRO(Comm_dup_with_info) \
  MACRO(Comm_free_keyval) \
  MACRO(Comm_free) \
  MACRO(Comm_get_attr) \
  MACRO(Dist_graph_create) \
  MACRO(Dist_graph_create_adjacent) \
  MACRO(Dist_graph_neighbors) \
  MACRO(Dist_graph_neighbors_count) \
  MACRO(Comm_get_errhandler) \
  MACRO(Comm_get_info) \
  MACRO(Comm_get_name) \
  MACRO(Comm_get_parent) \
  MACRO(Comm_group) \
  MACRO(Comm_join) \
  MACRO(Comm_rank) \
  MACRO(Comm_remote_group) \
  MACRO(Comm_remote_size) \
  MACRO(Comm_set_attr) \
  MACRO(Comm_set_errhandler) \
  MACRO(Comm_set_info) \
  MACRO(Comm_set_name) \
  MACRO(Comm_size) \
  MACRO(Comm_spawn) \
  MACRO(Comm_spawn_multiple) \
  MACRO(Comm_split_type) \
  MACRO(Comm_test_inter) \
  MACRO(Compare_and_swap) \
  MACRO(Dims_create) \
  MACRO(Errhandler_create) \
  MACRO(Errhandler_free) \
  MACRO(Errhandler_get) \
  MACRO(Errhandler_set) \
  MACRO(Error_class) \
  MACRO(Error_string) \
  MACRO(Exscan) \
  MACRO(Fetch_and_op) \
  MACRO(Iexscan) \
  MACRO(File_call_errhandler) \
  MACRO(File_create_errhandler) \
  MACRO(File_set_errhandler) \
  MACRO(File_get_errhandler) \
  MACRO(File_open) \
  MACRO(File_close) \
  MACRO(File_delete) \
  MACRO(File_set_size) \
  MACRO(File_preallocate) \
  MACRO(File_get_size) \
  MACRO(File_get_group) \
  MACRO(File_get_amode) \
  MACRO(File_set_info) \
  MACRO(File_get_info) \
  MACRO(File_set_view) \
  MACRO(File_get_view) \
  MACRO(File_read_at) \
  MACRO(File_read_at_all) \
  MACRO(File_write_at) \
  MACRO(File_write_at_all) \
  MACRO(File_iread_at) \
  MACRO(File_iwrite_at) \
  MACRO(File_iread_at_all) \
  MACRO(File_iwrite_at_all) \
  MACRO(File_read) \
  MACRO(File_read_all) \
  MACRO(File_write) \
  MACRO(File_write_all) \
  MACRO(File_iread) \
  MACRO(File_iwrite) \
  MACRO(File_iread_all) \
  MACRO(File_iwrite_all) \
  MACRO(File_seek) \
  MACRO(File_get_position) \
  MACRO(File_get_byte_offset) \
  MACRO(File_read_shared) \
  MACRO(File_write_shared) \
  MACRO(File_iread_shared) \
  MACRO(File_iwrite_shared) \
  MACRO(File_read_ordered) \
  MACRO(File_write_ordered) \
  MACRO(File_seek_shared) \
  MACRO(File_get_position_shared) \
  MACRO(File_read_at_all_begin) \
  MACRO(File_read_at_all_end) \
  MACRO(File_write_at_all_begin) \
  MACRO(File_write_at_all_end) \
  MACRO(File_read_all_begin) \
  MACRO(File_read_all_end) \
  MACRO(File_write_all_begin) \
  MACRO(File_write_all_end) \
  MACRO(File_read_ordered_begin) \
  MACRO(File_read_ordered_end) \
  MACRO(File_write_ordered_begin) \
  MACRO(File_write_ordered_end) \
  MACRO(File_get_type_extent) \
  MACRO(File_set_atomicity) \
  MACRO(File_get_atomicity) \
  MACRO(File_sync) \
  MACRO(Finalized) \
  MACRO(Free_mem) \
  MACRO(Gather) \
  MACRO(Igather) \
  MACRO(Gatherv) \
  MACRO(Igatherv) \
  MACRO(Get_address) \
  MACRO(Get_elements) \
  MACRO(Get_elements_x) \
  MACRO(Get) \
  MACRO(Get_accumulate) \
  MACRO(Get_library_version) \
  MACRO(Get_processor_name) \
  MACRO(Get_version) \
  MACRO(Graph_create) \
  MACRO(Graph_get) \
  MACRO(Graph_map) \
  MACRO(Graph_neighbors_count) \
  MACRO(Graph_neighbors) \
  MACRO(Graphdims_get) \
  MACRO(Grequest_complete) \
  MACRO(Group_compare) \
  MACRO(Group_difference) \
  MACRO(Group_excl) \
  MACRO(Group_free) \
  MACRO(Group_incl) \
  MACRO(Group_intersection) \
  MACRO(Group_range_excl) \
  MACRO(Group_range_incl) \
  MACRO(Group_rank) \
  MACRO(Group_size) \
  MACRO(Group_translate_ranks) \
  MACRO(Group_union) \
  MACRO(Ibsend) \
  MACRO(Improbe) \
  MACRO(Imrecv) \
  MACRO(Info_create) \
  MACRO(Info_delete) \
  MACRO(Info_dup) \
  MACRO(Info_free) \
  MACRO(Info_get) \
  MACRO(Info_get_nkeys) \
  MACRO(Info_get_nthkey) \
  MACRO(Info_get_valuelen) \
  MACRO(Info_set) \
  MACRO(Initialized) \
  MACRO(Init_thread) \
  MACRO(Intercomm_create) \
  MACRO(Intercomm_merge) \
  MACRO(Irsend) \
  MACRO(Issend) \
  MACRO(Is_thread_main) \
  MACRO(Keyval_create) \
  MACRO(Keyval_free) \
  MACRO(Lookup_name) \
  MACRO(Mprobe) \
  MACRO(Mrecv) \
  MACRO(Neighbor_allgather) \
  MACRO(Ineighbor_allgather) \
  MACRO(Neighbor_allgatherv) \
  MACRO(Ineighbor_allgatherv) \
  MACRO(Neighbor_alltoall) \
  MACRO(Ineighbor_alltoall) \
  MACRO(Neighbor_alltoallv) \
  MACRO(Ineighbor_alltoallv) \
  MACRO(Neighbor_alltoallw) \
  MACRO(Ineighbor_alltoallw) \
  MACRO(Op_commutative) \
  MACRO(Op_create) \
  MACRO(Open_port) \
  MACRO(Op_free) \
  MACRO(Pack_external) \
  MACRO(Pack_external_size) \
  MACRO(Pack) \
  MACRO(Pack_size) \
  MACRO(Pcontrol) \
  MACRO(Probe) \
  MACRO(Publish_name) \
  MACRO(Put) \
  MACRO(Query_thread) \
  MACRO(Raccumulate) \
  MACRO(Recv_init) \
  MACRO(Ireduce) \
  MACRO(Reduce_local) \
  MACRO(Reduce_scatter) \
  MACRO(Ireduce_scatter) \
  MACRO(Reduce_scatter_block) \
  MACRO(Ireduce_scatter_block) \
  MACRO(Register_datarep) \
  MACRO(Request_free) \
  MACRO(Request_get_status) \
  MACRO(Rget) \
  MACRO(Rget_accumulate) \
  MACRO(Rput) \
  MACRO(Rsend) \
  MACRO(Rsend_init) \
  MACRO(Scan) \
  MACRO(Iscan) \
  MACRO(Scatter) \
  MACRO(Iscatter) \
  MACRO(Scatterv) \
  MACRO(Iscatterv) \
  MACRO(Send_init) \
  MACRO(Sendrecv) \
  MACRO(Sendrecv_replace) \
  MACRO(Ssend_init) \
  MACRO(Ssend) \
  MACRO(Start) \
  MACRO(Startall) \
  MACRO(Status_set_cancelled) \
  MACRO(Status_set_elements) \
  MACRO(Status_set_elements_x) \
  MACRO(Testall) \
  MACRO(Testany) \
  MACRO(Test_cancelled) \
  MACRO(Testsome) \
  MACRO(Topo_test) \
  MACRO(Type_commit) \
  MACRO(Type_contiguous) \
  MACRO(Type_create_darray) \
  MACRO(Type_create_f90_complex) \
  MACRO(Type_create_f90_integer) \
  MACRO(Type_create_f90_real) \
  MACRO(Type_create_hindexed_block) \
  MACRO(Type_create_hindexed) \
  MACRO(Type_create_hvector) \
  MACRO(Type_create_keyval) \
  MACRO(Type_create_indexed_block) \
  MACRO(Type_create_struct) \
  MACRO(Type_create_subarray) \
  MACRO(Type_create_resized) \
  MACRO(Type_delete_attr) \
  MACRO(Type_dup) \
  MACRO(Type_extent) \
  MACRO(Type_free) \
  MACRO(Type_free_keyval) \
  MACRO(Type_get_attr) \
  MACRO(Type_get_contents) \
  MACRO(Type_get_envelope) \
  MACRO(Type_get_extent) \
  MACRO(Type_get_extent_x) \
  MACRO(Type_get_name) \
  MACRO(Type_get_true_extent) \
  MACRO(Type_get_true_extent_x) \
  MACRO(Type_hindexed) \
  MACRO(Type_hvector) \
  MACRO(Type_indexed) \
  MACRO(Type_lb) \
  MACRO(Type_match_size) \
  MACRO(Type_set_attr) \
  MACRO(Type_set_name) \
  MACRO(Type_size_x) \
  MACRO(Type_struct) \
  MACRO(Type_ub) \
  MACRO(Type_vector) \
  MACRO(Unpack) \
  MACRO(Unpublish_name) \
  MACRO(Unpack_external ) \
  MACRO(Waitall) \
  MACRO(Waitany) \
  MACRO(Waitsome) \
  MACRO(Win_allocate) \
  MACRO(Win_allocate_shared) \
  MACRO(Win_attach) \
  MACRO(Win_call_errhandler) \
  MACRO(Win_complete) \
  MACRO(Win_create) \
  MACRO(Win_create_dynamic) \
  MACRO(Win_create_errhandler) \
  MACRO(Win_create_keyval) \
  MACRO(Win_delete_attr) \
  MACRO(Win_detach) \
  MACRO(Win_fence) \
  MACRO(Win_flush) \
  MACRO(Win_flush_all) \
  MACRO(Win_flush_local) \
  MACRO(Win_flush_local_all) \
  MACRO(Win_free) \
  MACRO(Win_free_keyval) \
  MACRO(Win_get_attr) \
  MACRO(Win_get_errhandler) \
  MACRO(Win_get_group) \
  MACRO(Win_get_info) \
  MACRO(Win_get_name) \
  MACRO(Win_lock) \
  MACRO(Win_lock_all) \
  MACRO(Win_post) \
  MACRO(Win_set_attr) \
  MACRO(Win_set_errhandler) \
  MACRO(Win_set_info) \
  MACRO(Win_set_name) \
  MACRO(Win_shared_query) \
  MACRO(Win_start) \
  MACRO(Win_sync) \
  MACRO(Win_test) \
  MACRO(Win_unlock) \
  MACRO(Win_unlock_all) \
  MACRO(Win_wait) \
  MACRO(Wtick) \
  MACRO(Wtime) \
  MACRO(MANA_Internal)

#define GENERATE_ENUM(ENUM) MPI_Fnc_##ENUM,
#define GENERATE_FNC_PTR(FNC) (void*)&MPI_##FNC,
#define GENERATE_FNC_STRING(FNC)  "MPI_" #FNC

enum MPI_Fncs {
  MPI_Fnc_NULL,
  FOREACH_FNC(GENERATE_ENUM)
  MPI_Fnc_Invalid,
};
static const char *MPI_Fnc_strings[] = {
  "MPI_Fnc_NULL",
  FOREACH_FNC(GENERATE_FNC_STRING)
  "MPI_Fnc_Invalid"
};

void* lh_dlsym(enum MPI_Fncs fnc);
typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
extern proxyDlsym_t pdlsym;
std::vector<MmapInfo_t> &get_mmapped_list(int *num);
typedef std::vector<MmapInfo_t>& (*get_mmapped_list_fptr_t)(int *num);

#endif // ifndef _LOWER_HALF_API_H
