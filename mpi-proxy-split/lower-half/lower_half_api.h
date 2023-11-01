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

#include <stdint.h>

// This file is shared between lh_proxy (lower-half directory)
// and mtcp_restart (restart_plugin directory).  We copy this file
// from lower-half to restart_plugin in the mpi-proxy-splitMakefile.

typedef struct __MemRange
{
  void *start;  // Start of the address range for lower half memory allocations
  void *end;    // End of the address range for lower half memory allocations
} MemRange_t;

typedef struct __MmapInfo
{
  void *addr;   // Start address of mmapped region
  size_t len;   // Length (in bytes) of mmapped region
  int unmapped; // 1 if the region was unmapped; 0 otherwise
  int dontuse;  // 1 if preexisting mmap in region (e.g., xpmem); 0 otherwise
  int guard;    // 1 if the region has additional guard pages around it; 0 otherwise
} MmapInfo_t;

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
  void *libc_start_main; // Pointer to libc's __libc_start_main function in statically-linked lower half
  void *main;      // Pointer to the main() function in statically-linked lower half
  void *libc_csu_init; // Pointer to libc's __libc_csu_init() function in statically-linked lower half
  void *libc_csu_fini; // Pointer ot libc's __libc_csu_fini() function in statically-linked lower half
  void *fsaddr; // The base value of the FS register of the lower half
  uint64_t lh_AT_PHNUM; // The number of program headers (AT_PHNUM) from the auxiliary vector of the lower half
  uint64_t lh_AT_PHDR;  // The address of the program headers (AT_PHDR) from the auxiliary vector of the lower half
  void *g_appContext; // Pointer to ucontext_t of upper half application (defined in the lower half)
  void *lh_dlsym;     // Pointer to mydlsym() function in the lower half
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
  MemRange_t memRange; // MemRange_t object in the lower half
} LowerHalfInfo_t;

/* Maximum core regions lh_regions_list can store */
#define MAX_LH_REGIONS 500
extern LhCoreRegions_t lh_regions_list[MAX_LH_REGIONS];

// startProxy() (called from splitProcess()) will initialize 'lh_info'
extern LowerHalfInfo_t lh_info;  
extern LowerHalfInfo_t *lh_info_addr;  


#if 0
// Pointer to the custom dlsym implementation (see mydlsym() in libproxy.c) in
// the lower half. This is initialized using the information passed to us by
// the transient lh_proxy process in DMTCP_EVENT_INIT.
// initializeLowerHalf() will initialize this to: (proxyDlsym_t)lh_info.lh_dlsym
typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
extern proxyDlsym_t pdlsym;
#endif

#endif // ifndef _LOWER_HALF_API_H
