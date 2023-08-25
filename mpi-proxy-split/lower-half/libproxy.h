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

#define FOREACH_CONSTANT(MACRO) \
  MACRO(MPI_GROUP_NULL) \
  MACRO(MPI_COMM_NULL) \
  MACRO(MPI_REQUEST_NULL) \
  MACRO(MPI_OP_NULL) \
  MACRO(MPI_INFO_NULL) \
  MACRO(MPI_COMM_WORLD) \
  MACRO(MPI_COMM_SELF) \
  MACRO(MPI_GROUP_EMPTY) \
  MACRO(MPI_MAX) \
  MACRO(MPI_MIN) \
  MACRO(MPI_SUM) \
  MACRO(MPI_PROD) \
  MACRO(MPI_LAND) \
  MACRO(MPI_BAND) \
  MACRO(MPI_LOR) \
  MACRO(MPI_BOR) \
  MACRO(MPI_MAXLOC) \
  MACRO(MPI_MINLOC) \
  MACRO(MPI_DATATYPE_NULL) \
  MACRO(MPI_BYTE) \
  MACRO(MPI_PACKED) \
  MACRO(MPI_CHAR) \
  MACRO(MPI_SHORT) \
  MACRO(MPI_INT) \
  MACRO(MPI_LONG) \
  MACRO(MPI_FLOAT) \
  MACRO(MPI_DOUBLE) \
  MACRO(MPI_LONG_DOUBLE) \
  MACRO(MPI_UNSIGNED_CHAR) \
  MACRO(MPI_SIGNED_CHAR) \
  MACRO(MPI_UNSIGNED_SHORT) \
  MACRO(MPI_UNSIGNED_LONG) \
  MACRO(MPI_UNSIGNED) \
  MACRO(MPI_FLOAT_INT) \
  MACRO(MPI_DOUBLE_INT) \
  MACRO(MPI_LONG_DOUBLE_INT) \
  MACRO(MPI_LONG_INT) \
  MACRO(MPI_SHORT_INT) \
  MACRO(MPI_2INT) \
  MACRO(MPI_WCHAR) \
  MACRO(MPI_LONG_LONG_INT) \
  MACRO(MPI_LONG_LONG) \
  MACRO(MPI_UNSIGNED_LONG_LONG) \
  MACRO(MPI_INT8_T) \
  MACRO(MPI_UINT8_T) \
  MACRO(MPI_INT16_T) \
  MACRO(MPI_UINT16_T) \
  MACRO(MPI_INT32_T) \
  MACRO(MPI_UINT32_T) \
  MACRO(MPI_INT64_T) \
  MACRO(MPI_UINT64_T) \
  MACRO(MPI_AINT) \
  MACRO(MPI_CXX_BOOL) \
  MACRO(MPI_CXX_FLOAT_COMPLEX) \
  MACRO(MPI_CXX_DOUBLE_COMPLEX) \
  MACRO(MPI_CXX_LONG_DOUBLE_COMPLEX) \

#endif // define _LIBPROXY_H
