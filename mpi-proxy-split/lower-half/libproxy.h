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

// Included to retrieve MPICH, OPEN_MPI, EXAMPI
#include <mpi.h>

#ifndef _LIBPROXY_H
#define _LIBPROXY_H

#ifndef DEBUG_LEVEL
# define DEBUG_LEVEL 0
#endif // ifndef DEBUG_LEVEL

#define VA_ARGS(...)  , ##__VA_ARGS__
#define DLOG(LOG_LEVEL, fmt, ...)                                              \
do {                                                                           \
  if (DEBUG_LEVEL) {                                                           \
    if (LOG_LEVEL <= DEBUG_LEVEL)                                              \
      fprintf(stderr, "[%s +%d]: " fmt, __FILE__,                              \
              __LINE__ VA_ARGS(__VA_ARGS__));                                  \
  }                                                                            \
} while(0)

#define NOISE 3 // Noise!
#define INFO  2 // Informational logs
#define ERROR 1 // Highest error/exception level

#if defined(MPICH)
#define FOREACH_FNC(MACRO) \
  MACRO(Init), \
  MACRO(Finalize), \
  MACRO(Send), \
  MACRO(Recv), \
  MACRO(Type_size), \
  MACRO(Iprobe), \
  MACRO(Get_count), \
  MACRO(Isend), \
  MACRO(Irecv), \
  MACRO(Wait), \
  MACRO(Test), \
  MACRO(Bcast), \
  MACRO(Abort), \
  MACRO(Barrier), \
  MACRO(Reduce), \
  MACRO(Allreduce), \
  MACRO(Alltoall), \
  MACRO(Alltoallv), \
  MACRO(Comm_split), \
  MACRO(Accumulate), \
  MACRO(Add_error_class), \
  MACRO(Add_error_code), \
  MACRO(Add_error_string), \
  MACRO(Address), \
  MACRO(Allgather), \
  MACRO(Iallgather), \
  MACRO(Allgatherv), \
  MACRO(Iallgatherv), \
  MACRO(Alloc_mem), \
  MACRO(Iallreduce), \
  MACRO(Ialltoall), \
  MACRO(Ialltoallv), \
  MACRO(Alltoallw), \
  MACRO(Ialltoallw), \
  MACRO(Attr_delete), \
  MACRO(Attr_get), \
  MACRO(Attr_put), \
  MACRO(Ibarrier), \
  MACRO(Bsend), \
  MACRO(Ibcast), \
  MACRO(Bsend_init), \
  MACRO(Buffer_attach), \
  MACRO(Buffer_detach), \
  MACRO(Cancel), \
  MACRO(Cart_coords), \
  MACRO(Cart_create), \
  MACRO(Cart_get), \
  MACRO(Cart_map), \
  MACRO(Cart_rank), \
  MACRO(Cart_shift), \
  MACRO(Cart_sub), \
  MACRO(Cartdim_get), \
  MACRO(Close_port), \
  MACRO(Comm_accept), \
  MACRO(Comm_call_errhandler), \
  MACRO(Comm_compare), \
  MACRO(Comm_connect), \
  MACRO(Comm_create_errhandler), \
  MACRO(Comm_create_keyval), \
  MACRO(Comm_create_group), \
  MACRO(Comm_create), \
  MACRO(Comm_delete_attr), \
  MACRO(Comm_disconnect), \
  MACRO(Comm_dup), \
  MACRO(Comm_idup), \
  MACRO(Comm_dup_with_info), \
  MACRO(Comm_free_keyval), \
  MACRO(Comm_free), \
  MACRO(Comm_get_attr), \
  MACRO(Dist_graph_create), \
  MACRO(Dist_graph_create_adjacent), \
  MACRO(Dist_graph_neighbors), \
  MACRO(Dist_graph_neighbors_count), \
  MACRO(Comm_get_errhandler), \
  MACRO(Comm_get_info), \
  MACRO(Comm_get_name), \
  MACRO(Comm_get_parent), \
  MACRO(Comm_group), \
  MACRO(Comm_join), \
  MACRO(Comm_rank), \
  MACRO(Comm_remote_group), \
  MACRO(Comm_remote_size), \
  MACRO(Comm_set_attr), \
  MACRO(Comm_set_errhandler), \
  MACRO(Comm_set_info), \
  MACRO(Comm_set_name), \
  MACRO(Comm_size), \
  MACRO(Comm_spawn), \
  MACRO(Comm_spawn_multiple), \
  MACRO(Comm_split_type), \
  MACRO(Comm_test_inter), \
  MACRO(Compare_and_swap), \
  MACRO(Dims_create), \
  MACRO(Errhandler_create), \
  MACRO(Errhandler_free), \
  MACRO(Errhandler_get), \
  MACRO(Errhandler_set), \
  MACRO(Error_class), \
  MACRO(Error_string), \
  MACRO(Exscan), \
  MACRO(Fetch_and_op), \
  MACRO(Iexscan), \
  MACRO(File_call_errhandler), \
  MACRO(File_create_errhandler), \
  MACRO(File_set_errhandler), \
  MACRO(File_get_errhandler), \
  MACRO(File_open), \
  MACRO(File_close), \
  MACRO(File_delete), \
  MACRO(File_set_size), \
  MACRO(File_preallocate), \
  MACRO(File_get_size), \
  MACRO(File_get_group), \
  MACRO(File_get_amode), \
  MACRO(File_set_info), \
  MACRO(File_get_info), \
  MACRO(File_set_view), \
  MACRO(File_get_view), \
  MACRO(File_read_at), \
  MACRO(File_read_at_all), \
  MACRO(File_write_at), \
  MACRO(File_write_at_all), \
  MACRO(File_iread_at), \
  MACRO(File_iwrite_at), \
  MACRO(File_iread_at_all), \
  MACRO(File_iwrite_at_all), \
  MACRO(File_read), \
  MACRO(File_read_all), \
  MACRO(File_write), \
  MACRO(File_write_all), \
  MACRO(File_iread), \
  MACRO(File_iwrite), \
  MACRO(File_iread_all), \
  MACRO(File_iwrite_all), \
  MACRO(File_seek), \
  MACRO(File_get_position), \
  MACRO(File_get_byte_offset), \
  MACRO(File_read_shared), \
  MACRO(File_write_shared), \
  MACRO(File_iread_shared), \
  MACRO(File_iwrite_shared), \
  MACRO(File_read_ordered), \
  MACRO(File_write_ordered), \
  MACRO(File_seek_shared), \
  MACRO(File_get_position_shared), \
  MACRO(File_read_at_all_begin), \
  MACRO(File_read_at_all_end), \
  MACRO(File_write_at_all_begin), \
  MACRO(File_write_at_all_end), \
  MACRO(File_read_all_begin), \
  MACRO(File_read_all_end), \
  MACRO(File_write_all_begin), \
  MACRO(File_write_all_end), \
  MACRO(File_read_ordered_begin), \
  MACRO(File_read_ordered_end), \
  MACRO(File_write_ordered_begin), \
  MACRO(File_write_ordered_end), \
  MACRO(File_get_type_extent), \
  MACRO(File_set_atomicity), \
  MACRO(File_get_atomicity), \
  MACRO(File_sync), \
  MACRO(Finalized), \
  MACRO(Free_mem), \
  MACRO(Gather), \
  MACRO(Igather), \
  MACRO(Gatherv), \
  MACRO(Igatherv), \
  MACRO(Get_address), \
  MACRO(Get_elements), \
  MACRO(Get_elements_x), \
  MACRO(Get), \
  MACRO(Get_accumulate), \
  MACRO(Get_library_version), \
  MACRO(Get_processor_name), \
  MACRO(Get_version), \
  MACRO(Graph_create), \
  MACRO(Graph_get), \
  MACRO(Graph_map), \
  MACRO(Graph_neighbors_count), \
  MACRO(Graph_neighbors), \
  MACRO(Graphdims_get), \
  MACRO(Grequest_complete), \
  MACRO(Group_compare), \
  MACRO(Group_difference), \
  MACRO(Group_excl), \
  MACRO(Group_free), \
  MACRO(Group_incl), \
  MACRO(Group_intersection), \
  MACRO(Group_range_excl), \
  MACRO(Group_range_incl), \
  MACRO(Group_rank), \
  MACRO(Group_size), \
  MACRO(Group_translate_ranks), \
  MACRO(Group_union), \
  MACRO(Ibsend), \
  MACRO(Improbe), \
  MACRO(Imrecv), \
  MACRO(Info_create), \
  MACRO(Info_delete), \
  MACRO(Info_dup), \
  MACRO(Info_free), \
  MACRO(Info_get), \
  MACRO(Info_get_nkeys), \
  MACRO(Info_get_nthkey), \
  MACRO(Info_get_valuelen), \
  MACRO(Info_set), \
  MACRO(Initialized), \
  MACRO(Init_thread), \
  MACRO(Intercomm_create), \
  MACRO(Intercomm_merge), \
  MACRO(Irsend), \
  MACRO(Issend), \
  MACRO(Is_thread_main), \
  MACRO(Keyval_create), \
  MACRO(Keyval_free), \
  MACRO(Lookup_name), \
  MACRO(Mprobe), \
  MACRO(Mrecv), \
  MACRO(Neighbor_allgather), \
  MACRO(Ineighbor_allgather), \
  MACRO(Neighbor_allgatherv), \
  MACRO(Ineighbor_allgatherv), \
  MACRO(Neighbor_alltoall), \
  MACRO(Ineighbor_alltoall), \
  MACRO(Neighbor_alltoallv), \
  MACRO(Ineighbor_alltoallv), \
  MACRO(Neighbor_alltoallw), \
  MACRO(Ineighbor_alltoallw), \
  MACRO(Op_commutative), \
  MACRO(Op_create), \
  MACRO(Open_port), \
  MACRO(Op_free), \
  MACRO(Pack_external), \
  MACRO(Pack_external_size), \
  MACRO(Pack), \
  MACRO(Pack_size), \
  MACRO(Pcontrol), \
  MACRO(Probe), \
  MACRO(Publish_name), \
  MACRO(Put), \
  MACRO(Query_thread), \
  MACRO(Raccumulate), \
  MACRO(Recv_init), \
  MACRO(Ireduce), \
  MACRO(Reduce_local), \
  MACRO(Reduce_scatter), \
  MACRO(Ireduce_scatter), \
  MACRO(Reduce_scatter_block), \
  MACRO(Ireduce_scatter_block), \
  MACRO(Register_datarep), \
  MACRO(Request_free), \
  MACRO(Request_get_status), \
  MACRO(Rget), \
  MACRO(Rget_accumulate), \
  MACRO(Rput), \
  MACRO(Rsend), \
  MACRO(Rsend_init), \
  MACRO(Scan), \
  MACRO(Iscan), \
  MACRO(Scatter), \
  MACRO(Iscatter), \
  MACRO(Scatterv), \
  MACRO(Iscatterv), \
  MACRO(Send_init), \
  MACRO(Sendrecv), \
  MACRO(Sendrecv_replace), \
  MACRO(Ssend_init), \
  MACRO(Ssend), \
  MACRO(Start), \
  MACRO(Startall), \
  MACRO(Status_set_cancelled), \
  MACRO(Status_set_elements), \
  MACRO(Status_set_elements_x), \
  MACRO(Testall), \
  MACRO(Testany), \
  MACRO(Test_cancelled), \
  MACRO(Testsome), \
  MACRO(Topo_test), \
  MACRO(Type_commit), \
  MACRO(Type_contiguous), \
  MACRO(Type_create_darray), \
  MACRO(Type_create_f90_complex), \
  MACRO(Type_create_f90_integer), \
  MACRO(Type_create_f90_real), \
  MACRO(Type_create_hindexed_block), \
  MACRO(Type_create_hindexed), \
  MACRO(Type_create_hvector), \
  MACRO(Type_create_keyval), \
  MACRO(Type_create_indexed_block), \
  MACRO(Type_create_struct), \
  MACRO(Type_create_subarray), \
  MACRO(Type_create_resized), \
  MACRO(Type_delete_attr), \
  MACRO(Type_dup), \
  MACRO(Type_extent), \
  MACRO(Type_free), \
  MACRO(Type_free_keyval), \
  MACRO(Type_get_attr), \
  MACRO(Type_get_contents), \
  MACRO(Type_get_envelope), \
  MACRO(Type_get_extent), \
  MACRO(Type_get_extent_x), \
  MACRO(Type_get_name), \
  MACRO(Type_get_true_extent), \
  MACRO(Type_get_true_extent_x), \
  MACRO(Type_hindexed), \
  MACRO(Type_hvector), \
  MACRO(Type_indexed), \
  MACRO(Type_lb), \
  MACRO(Type_match_size), \
  MACRO(Type_set_attr), \
  MACRO(Type_set_name), \
  MACRO(Type_size_x), \
  MACRO(Type_struct), \
  MACRO(Type_ub), \
  MACRO(Type_vector), \
  MACRO(Unpack), \
  MACRO(Unpublish_name), \
  MACRO(Unpack_external ), \
  MACRO(Waitall), \
  MACRO(Waitany), \
  MACRO(Waitsome), \
  MACRO(Win_allocate), \
  MACRO(Win_allocate_shared), \
  MACRO(Win_attach), \
  MACRO(Win_call_errhandler), \
  MACRO(Win_complete), \
  MACRO(Win_create), \
  MACRO(Win_create_dynamic), \
  MACRO(Win_create_errhandler), \
  MACRO(Win_create_keyval), \
  MACRO(Win_delete_attr), \
  MACRO(Win_detach), \
  MACRO(Win_fence), \
  MACRO(Win_flush), \
  MACRO(Win_flush_all), \
  MACRO(Win_flush_local), \
  MACRO(Win_flush_local_all), \
  MACRO(Win_free), \
  MACRO(Win_free_keyval), \
  MACRO(Win_get_attr), \
  MACRO(Win_get_errhandler), \
  MACRO(Win_get_group), \
  MACRO(Win_get_info), \
  MACRO(Win_get_name), \
  MACRO(Win_lock), \
  MACRO(Win_lock_all), \
  MACRO(Win_post), \
  MACRO(Win_set_attr), \
  MACRO(Win_set_errhandler), \
  MACRO(Win_set_info), \
  MACRO(Win_set_name), \
  MACRO(Win_shared_query), \
  MACRO(Win_start), \
  MACRO(Win_sync), \
  MACRO(Win_test), \
  MACRO(Win_unlock), \
  MACRO(Win_unlock_all), \
  MACRO(Win_wait), \
  MACRO(Wtick), \
  MACRO(Wtime), \
  MACRO(MANA_Internal), \
  MACRO(Aint_diff),
#elif defined(OPEN_MPI)
#define FOREACH_FNC(MACRO) \
  MACRO(Init), \
  MACRO(Finalize), \
  MACRO(Send), \
  MACRO(Recv), \
  MACRO(Type_size), \
  MACRO(Iprobe), \
  MACRO(Get_count), \
  MACRO(Isend), \
  MACRO(Irecv), \
  MACRO(Wait), \
  MACRO(Test), \
  MACRO(Bcast), \
  MACRO(Abort), \
  MACRO(Barrier), \
  MACRO(Reduce), \
  MACRO(Allreduce), \
  MACRO(Alltoall), \
  MACRO(Alltoallv), \
  MACRO(Comm_split), \
  MACRO(Add_error_class), \
  MACRO(Add_error_code), \
  MACRO(Add_error_string), \
  MACRO(Allgather), \
  MACRO(Iallgather), \
  MACRO(Allgatherv), \
  MACRO(Iallgatherv), \
  MACRO(Iallreduce), \
  MACRO(Ialltoall), \
  MACRO(Ialltoallv), \
  MACRO(Alltoallw), \
  MACRO(Ialltoallw), \
  MACRO(Bsend), \
  MACRO(Ibcast), \
  MACRO(Bsend_init), \
  MACRO(Cancel), \
  MACRO(Cart_coords), \
  MACRO(Cart_create), \
  MACRO(Cart_get), \
  MACRO(Cart_rank), \
  MACRO(Cart_shift), \
  MACRO(Cart_sub), \
  MACRO(Comm_compare), \
  MACRO(Comm_create_group), \
  MACRO(Comm_create), \
  MACRO(Comm_dup), \
  MACRO(Comm_free), \
  MACRO(Comm_get_name), \
  MACRO(Comm_group), \
  MACRO(Comm_rank), \
  MACRO(Comm_remote_group), \
  MACRO(Comm_remote_size), \
  MACRO(Comm_set_errhandler), \
  MACRO(Comm_set_name), \
  MACRO(Comm_size), \
  MACRO(Comm_split_type), \
  MACRO(Comm_test_inter), \
  MACRO(Error_class), \
  MACRO(Error_string), \
  MACRO(Exscan), \
  MACRO(Iexscan), \
  MACRO(Finalized), \
  MACRO(Gather), \
  MACRO(Igather), \
  MACRO(Gatherv), \
  MACRO(Igatherv), \
  MACRO(Get_address), \
  MACRO(Get_library_version), \
  MACRO(Get_processor_name), \
  MACRO(Get_version), \
  MACRO(Group_compare), \
  MACRO(Group_difference), \
  MACRO(Group_excl), \
  MACRO(Group_free), \
  MACRO(Group_incl), \
  MACRO(Group_intersection), \
  MACRO(Group_rank), \
  MACRO(Group_size), \
  MACRO(Group_translate_ranks), \
  MACRO(Group_union), \
  MACRO(Ibsend), \
  MACRO(Info_create), \
  MACRO(Info_delete), \
  MACRO(Info_dup), \
  MACRO(Info_free), \
  MACRO(Info_get), \
  MACRO(Info_get_nkeys), \
  MACRO(Info_get_nthkey), \
  MACRO(Info_get_valuelen), \
  MACRO(Info_set), \
  MACRO(Initialized), \
  MACRO(Init_thread), \
  MACRO(Irsend), \
  MACRO(Issend), \
  MACRO(Is_thread_main), \
  MACRO(Op_create), \
  MACRO(Op_free), \
  MACRO(Pack), \
  MACRO(Pack_size), \
  MACRO(Probe), \
  MACRO(Recv_init), \
  MACRO(Ireduce), \
  MACRO(Request_free), \
  MACRO(Rsend), \
  MACRO(Rsend_init), \
  MACRO(Scan), \
  MACRO(Iscan), \
  MACRO(Scatter), \
  MACRO(Iscatter), \
  MACRO(Scatterv), \
  MACRO(Iscatterv), \
  MACRO(Send_init), \
  MACRO(Sendrecv), \
  MACRO(Ssend_init), \
  MACRO(Ssend), \
  MACRO(Start), \
  MACRO(Startall), \
  MACRO(Status_set_cancelled), \
  MACRO(Testall), \
  MACRO(Testany), \
  MACRO(Test_cancelled), \
  MACRO(Testsome), \
  MACRO(Topo_test), \
  MACRO(Type_commit), \
  MACRO(Type_contiguous), \
  MACRO(Type_create_hvector), \
  MACRO(Type_create_indexed_block), \
  MACRO(Type_create_struct), \
  MACRO(Type_create_subarray), \
  MACRO(Type_create_resized), \
  MACRO(Type_free), \
  MACRO(Type_get_extent), \
  MACRO(Type_get_name), \
  MACRO(Type_indexed), \
  MACRO(Type_set_name), \
  MACRO(Type_size_x), \
  MACRO(Type_vector), \
  MACRO(Unpack), \
  MACRO(Waitall), \
  MACRO(Waitany), \
  MACRO(Waitsome), \
  MACRO(Wtick), \
  MACRO(Wtime), \
  MACRO(MANA_Internal),
#elif defined(EXAMPI)
#define FOREACH_FNC(MACRO) \
  MACRO(Init), \
  MACRO(Finalize), \
  MACRO(Send), \
  MACRO(Recv), \
  MACRO(Type_size), \
  MACRO(Iprobe), \
  MACRO(Get_count), \
  MACRO(Isend), \
  MACRO(Irecv), \
  MACRO(Wait), \
  MACRO(Test), \
  MACRO(Bcast), \
  MACRO(Abort), \
  MACRO(Barrier), \
  MACRO(Reduce), \
  MACRO(Allreduce), \
  MACRO(Alltoall), \
  MACRO(Alltoallv), \
  MACRO(Comm_split), \
  MACRO(Add_error_class), \
  MACRO(Add_error_code), \
  MACRO(Add_error_string), \
  MACRO(Allgather), \
  MACRO(Iallgather), \
  MACRO(Allgatherv), \
  MACRO(Iallgatherv), \
  MACRO(Iallreduce), \
  MACRO(Ialltoall), \
  MACRO(Ialltoallv), \
  MACRO(Alltoallw), \
  MACRO(Ialltoallw), \
  MACRO(Bsend), \
  MACRO(Ibcast), \
  MACRO(Bsend_init), \
  MACRO(Cancel), \
  MACRO(Cart_coords), \
  MACRO(Cart_create), \
  MACRO(Cart_get), \
  MACRO(Cart_rank), \
  MACRO(Cart_shift), \
  MACRO(Cart_sub), \
  MACRO(Comm_compare), \
  MACRO(Comm_create_group), \
  MACRO(Comm_create), \
  MACRO(Comm_dup), \
  MACRO(Comm_free), \
  MACRO(Comm_get_name), \
  MACRO(Comm_group), \
  MACRO(Comm_rank), \
  MACRO(Comm_remote_group), \
  MACRO(Comm_remote_size), \
  MACRO(Comm_set_errhandler), \
  MACRO(Comm_set_name), \
  MACRO(Comm_size), \
  MACRO(Comm_split_type), \
  MACRO(Comm_test_inter), \
  MACRO(Error_class), \
  MACRO(Error_string), \
  MACRO(Exscan), \
  MACRO(Iexscan), \
  MACRO(Finalized), \
  MACRO(Gather), \
  MACRO(Igather), \
  MACRO(Gatherv), \
  MACRO(Igatherv), \
  MACRO(Get_address), \
  MACRO(Get_library_version), \
  MACRO(Get_processor_name), \
  MACRO(Get_version), \
  MACRO(Group_compare), \
  MACRO(Group_difference), \
  MACRO(Group_excl), \
  MACRO(Group_free), \
  MACRO(Group_incl), \
  MACRO(Group_intersection), \
  MACRO(Group_rank), \
  MACRO(Group_size), \
  MACRO(Group_translate_ranks), \
  MACRO(Group_union), \
  MACRO(Ibsend), \
  MACRO(Info_create), \
  MACRO(Info_delete), \
  MACRO(Info_dup), \
  MACRO(Info_free), \
  MACRO(Info_get), \
  MACRO(Info_get_nkeys), \
  MACRO(Info_get_nthkey), \
  MACRO(Info_get_valuelen), \
  MACRO(Info_set), \
  MACRO(Initialized), \
  MACRO(Init_thread), \
  MACRO(Irsend), \
  MACRO(Issend), \
  MACRO(Is_thread_main), \
  MACRO(Op_create), \
  MACRO(Op_free), \
  MACRO(Pack), \
  MACRO(Pack_size), \
  MACRO(Probe), \
  MACRO(Recv_init), \
  MACRO(Ireduce), \
  MACRO(Request_free), \
  MACRO(Rsend), \
  MACRO(Rsend_init), \
  MACRO(Scan), \
  MACRO(Iscan), \
  MACRO(Scatter), \
  MACRO(Iscatter), \
  MACRO(Scatterv), \
  MACRO(Iscatterv), \
  MACRO(Send_init), \
  MACRO(Sendrecv), \
  MACRO(Ssend_init), \
  MACRO(Ssend), \
  MACRO(Start), \
  MACRO(Startall), \
  MACRO(Status_set_cancelled), \
  MACRO(Testall), \
  MACRO(Testany), \
  MACRO(Test_cancelled), \
  MACRO(Testsome), \
  MACRO(Topo_test), \
  MACRO(Type_commit), \
  MACRO(Type_contiguous), \
  MACRO(Type_create_hvector), \
  MACRO(Type_create_indexed_block), \
  MACRO(Type_create_struct), \
  MACRO(Type_create_subarray), \
  MACRO(Type_create_resized), \
  MACRO(Type_free), \
  MACRO(Type_get_extent), \
  MACRO(Type_get_name), \
  MACRO(Type_indexed), \
  MACRO(Type_set_name), \
  MACRO(Type_size_x), \
  MACRO(Type_vector), \
  MACRO(Unpack), \
  MACRO(Waitall), \
  MACRO(Waitany), \
  MACRO(Waitsome), \
  MACRO(Wtick), \
  MACRO(Wtime), \
  MACRO(MANA_Internal),
#else
#error "Could not find an MPI implementation"
#endif // ifdef MPICH elseif OPEN_MPI elseif EXAMPI

#define FOREACH_CONSTANT(MACRO) \
  MACRO(GROUP_NULL) \
  MACRO(COMM_NULL) \
  MACRO(REQUEST_NULL) \
  MACRO(OP_NULL) \
  MACRO(INFO_NULL) \
  MACRO(COMM_WORLD) \
  MACRO(COMM_SELF) \
  MACRO(GROUP_EMPTY) \
  MACRO(MAX) \
  MACRO(MIN) \
  MACRO(SUM) \
  MACRO(PROD) \
  MACRO(BAND) \
  MACRO(LOR) \
  MACRO(BOR) \
  MACRO(MAXLOC) \
  MACRO(MINLOC) \
  MACRO(DATATYPE_NULL) \
  MACRO(BYTE) \
  MACRO(PACKED) \
  MACRO(CHAR) \
  MACRO(SHORT) \
  MACRO(INT) \
  MACRO(LONG) \
  MACRO(FLOAT) \
  MACRO(DOUBLE) \
  MACRO(LONG_DOUBLE) \
  MACRO(UNSIGNED_CHAR) \
  MACRO(SIGNED_CHAR) \
  MACRO(UNSIGNED_SHORT) \
  MACRO(UNSIGNED_LONG) \
  MACRO(UNSIGNED) \
  MACRO(FLOAT_INT) \
  MACRO(DOUBLE_INT) \
  MACRO(LONG_DOUBLE_INT) \
  MACRO(LONG_INT) \
  MACRO(SHORT_INT) \
  MACRO(2INT) \
  MACRO(WCHAR) \
  MACRO(LONG_LONG_INT) \
  MACRO(LONG_LONG) \
  MACRO(UNSIGNED_LONG_LONG) \
  MACRO(INT8_T) \
  MACRO(UINT8_T) \
  MACRO(INT16_T) \
  MACRO(UINT16_T) \
  MACRO(INT32_T) \
  MACRO(UINT32_T) \
  MACRO(INT64_T) \
  MACRO(UINT64_T) \
  MACRO(AINT) \
  MACRO(CXX_BOOL) \
  MACRO(CXX_FLOAT_COMPLEX) \
  MACRO(CXX_DOUBLE_COMPLEX) \
  MACRO(CXX_LONG_DOUBLE_COMPLEX) \
  MACRO(ERRORS_RETURN) \

#endif // define _LIBPROXY_H
