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

/* WARNING:  The original of this file is in the lower-half directory.
 * DO NOT EDIT THE VERSION IN THE restart_plugin DIRECTORY.
 */

#ifndef _LOWER_HALF_API_H
#define _LOWER_HALF_API_H

#include <mpi.h>
#include <stdint.h>
#include <stddef.h>

#include "switch_context.h"

extern "C" int MPI_MANA_Internal(char *dummy);

typedef char* VA;

typedef struct __MemRange
{
  void *start;  // Start of the address range for lower half memory allocations
  void *end;    // End of the address range for lower half memory allocations
} MemRange_t;

#if 0
typedef struct __MmapInfo
{
  void *addr;   // Start address of mmapped region
  size_t len;   // Length (in bytes) of mmapped region
  int unmapped; // 1 if the region was unmapped; 0 otherwise
  int dontuse;  // 1 if preexisting mmap in region (e.g., xpmem); 0 otherwise
  int guard;    // 1 if the region has additional guard pages around it; 0 otherwise
} MmapInfo_t;
#else
typedef struct __MmapInfo
{
  void *addr;
  size_t len;
} MmapInfo_t;
#endif

typedef struct __LhCoreRegions
{
  void * start_addr; // Start address of a LH memory segment
  void * end_addr; // End address
  int prot; // Protection flag
} LhCoreRegions_t;

// The transient lh_proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
// This must be same in restart_plugin and low
typedef struct _LowerHalfInfo
{
  void *startText; // Start address of text segment (R-X) of lower half
  void *endText;   // End address of text segmeent (R-X) of lower half
  void *endOfHeap; // Pointer to the end of heap segment of lower half
  int *endOfHeapFrozenAddr; // Pointer to boolean; Stopextending heap
  void *libc_start_main; // Pointer to libc's __libc_start_main function in statically-linked lower half
  void *main;      // Pointer to the main() function in statically-linked lower half
  void *libc_csu_init; // Pointer to libc's __libc_csu_init() function in statically-linked lower half
  void *libc_csu_fini; // Pointer ot libc's __libc_csu_fini() function in statically-linked lower half
  void *fsaddr; // The base value of the FS register of the lower half
  uint64_t lh_AT_PHNUM; // The number of program headers (AT_PHNUM) from the auxiliary vector of the lower half
  uint64_t lh_AT_PHDR;  // The address of the program headers (AT_PHDR) from the auxiliary vector of the lower half
  void *g_appContext; // Pointer to ucontext_t of upper half application (defined in the lower half)
  void *getRankFptr;  // Pointer to getRank() function in the lower half
#ifdef SINGLE_CART_REORDER
  void *getCoordinatesFptr; // Pointer to getCoordinates() function in the lower half
  void *getCartesianCommunicatorFptr; // Pointer to getCartesianCommunicator() function in the lower half
#endif
  void *parentStackStart; // Address to the start of the stack of the parent process (FIXME: Not currently used anywhere)
  void *updateEnvironFptr; // Pointer to updateEnviron() function in the lower half
  void *getMmappedListFptr; // Pointer to getMmappedList() function in the lower half
  void *resetMmappedListFptr; // Pointer to resetMmappedList() function in the lower half
  int numCoreRegions; // total number of core regions in the lower half
  LhCoreRegions_t *lh_regions_list;
  void *getLhRegionsListFptr; // Pointer to getLhRegionsList() function in the lower half
  void *vdsoLdAddrInLinkMap; // vDSO's LD address in the lower half's linkmap
  void *sbrk;
  void *mmap;
  void *munmap;
  void *lh_dlsym;
  void *mmap_list_fptr;
  void *uh_end_of_heap;
  MemRange_t memRange; // MemRange_t object in the lower half
} LowerHalfInfo_t;

/* Maximum core regions lh_regions_list can store */
#define MAX_LH_REGIONS 500
extern LhCoreRegions_t lh_regions_list[MAX_LH_REGIONS];

// startProxy() (called from splitProcess()) will initialize 'lh_info'
extern LowerHalfInfo_t lh_info;  
extern LowerHalfInfo_t *lh_info_addr;  

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
