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

#include "lower_half_api.h"

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
  MACRO(MANA_Internal), \
  MACRO(Type_get_contents), \
  MACRO(Type_get_envelope), \


// ===========================================================

#define GENERATE_ENUM(ENUM)    MPI_Fnc_##ENUM
#define GENERATE_FNC_PTR(FNC)  &MPI_##FNC
#define GENERATE_FNC_STRING(FNC)  "MPI_" #FNC
#define GENERATE_CONSTANT_ENUM(ENUM)    LH_##ENUM,
#define GENERATE_CONSTANT_VALUE(CONSTANT) CONSTANT

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL , void *
# define MAIN_AUXVEC_PARAM , auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif // ifdef MAIN_AUXVEC_ARG

enum MPI_Fncs {
  MPI_Fnc_NULL,
  FOREACH_FNC(GENERATE_ENUM)
  MPI_Fnc_Invalid,
};

enum MPI_Constants {
  LH_MPI_Constant_NULL,
  FOREACH_CONSTANT(GENERATE_CONSTANT_ENUM)
  LH_MPI_ERRORS_RETURN,
  LH_MPI_Constant_Invalid,
};

__attribute__ ((unused))
static const char *MPI_Fnc_strings[] = {
  "MPI_Fnc_NULL",
  FOREACH_FNC(GENERATE_FNC_STRING)
  "MPI_Fnc_Invalid"
};

// Useful type definitions

typedef int (*mainFptr)(int argc, char *argv[], char *envp[]);
typedef void (*finiFptr) (void);
typedef int (*libcFptr_t) (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
                           int ,
                           char **,
                           __typeof (main) ,
                           void (*fini) (void),
                           void (*rtld_fini) (void),
                           void *);

typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
typedef void* (*updateEnviron_t)(char **environ);
typedef void* (*lh_constant_t)(enum MPI_Constants constant);
typedef void (*resetMmappedList_t)();
typedef MmapInfo_t* (*getMmappedList_t)(int **num);
typedef LhCoreRegions_t* (*getLhRegionsList_t)(int *num);

// Global variables with lower-half information

// Pointer to the custom dlsym implementation (see mydlsym() in libproxy.c) in
// the lower half. This is initialized using the information passed to us by
// the transient lh_proxy process in DMTCP_EVENT_INIT.
// initializeLowerHalf() will initialize this to: (proxyDlsym_t)lh_info.lh_dlsym
extern proxyDlsym_t pdlsym;
extern lh_constant_t lh_mpi_constants;
extern LhCoreRegions_t lh_regions_list[MAX_LH_REGIONS];

// API




// Returns the address of an MPI API in the lower half's MPI library based on
// the given enum value
extern void *mydlsym(enum MPI_Fncs fnc);

// Initializes the MPI library in the lower half (by calling MPI_Init()) and
// returns the MPI rank of the current process
extern int getRank();

// Updates the lower half's global environ pointer (__environ) to the given
// 'newenviron' pointer value
extern void updateEnviron(const char **newenviron);

// Returns a pointer to the first element of a pre-allocated array of
// 'MmapInfo_t' objects and 'num' is set to the number of valid items in
// the array
extern MmapInfo_t* getMmappedList(int **num);

// Clears the global, pre-allocated array of 'MmapInfo_t' objects
extern void resetMmappedList();

extern LhCoreRegions_t* getLhRegionsList(int *num);

#endif // define _LIBPROXY_H
