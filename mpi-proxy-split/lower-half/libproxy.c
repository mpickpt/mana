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

// This could be libmpi.a or libproxy.a, with code to translate
//   between an MPI function and its address (similarly to dlsym()).
#ifndef _GNU_SOURCE
 #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <assert.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/auxv.h>
#include <linux/limits.h>
#include <mpi.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#ifdef SINGLE_CART_REORDER
#include "../cartesian.h"
#endif

extern int MPI_MANA_Internal(char *dummy);

#include "libproxy.h"
#include "mmap_internal.h"  // included for definition of ROUND_UP
#include "mpi_copybits.h"
#include "procmapsutils.h"
#include "lower_half_api.h"
#include "../mana_header.h"

LowerHalfInfo_t lh_info = {0};
// This is the allocated buffer for lh_info.memRange
MemRange_t lh_memRange = {0};
LhCoreRegions_t lh_core_regions[MAX_LH_REGIONS] = {0};
int totalRegions = 0;

static ucontext_t g_appContext;

static void* MPI_Fnc_Ptrs[] = {
  NULL,
  FOREACH_FNC(GENERATE_FNC_PTR)
  NULL,
};

// Local functions

LhCoreRegions_t*
getLhRegionsList(int *num)
{
  if (!num || *num > MAX_LH_REGIONS) return NULL;
  *num = totalRegions;
  return lh_core_regions;
}

static void
getDataFromMaps(const Area *text, Area *heap)
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  void *heap_sbrk = sbrk(0);
  int idx = 0;
  // For a static LH, mark all the regions till heap as core regions.
  // TODO: for a dynamic lower-half, core regions list will include libraries
  //       and libraries are usually mapped beyond the heap.
  while (readMapsLine(mapsfd, &area)) {
    lh_core_regions[idx].start_addr = area.addr;
    lh_core_regions[idx].end_addr = area.endAddr;
    lh_core_regions[idx].prot = area.prot;
    idx++;
    if (strstr(area.name, "[heap]") && area.endAddr >= (VA)heap_sbrk) {
      *heap = area;
      break;
    }
  }
  totalRegions = idx;
  close(mapsfd);
}

// FIXME: This code is duplicated in proxy and plugin. Refactor into utils.
static void
getTextSegmentRange(pid_t proc,                 // IN
                    unsigned long *start,       // OUT
                    unsigned long *end,         // OUT
                    unsigned long *stackstart)  // OUT
{
  // From man 5 proc: See entry for /proc/[pid]/stat
  int pid;
  char cmd[PATH_MAX]; char state;
  int ppid; int pgrp; int session; int tty_nr; int tpgid;
  unsigned flags;
  unsigned long minflt; unsigned long cminflt; unsigned long majflt;
  unsigned long cmajflt; unsigned long utime; unsigned long stime;
  long cutime; long cstime; long priority; long nice;
  long num_threads; long itrealvalue;
  unsigned long long starttime;
  unsigned long vsize;
  long rss;
  unsigned long rsslim; unsigned long startcode; unsigned long endcode;
  unsigned long startstack; unsigned long kstkesp; unsigned long kstkeip;
  unsigned long signal_map; unsigned long blocked; unsigned long sigignore;
  unsigned long sigcatch; unsigned long wchan; unsigned long nswap;
  unsigned long cnswap;
  int exit_signal; int processor;
  unsigned rt_priority; unsigned policy;

  FILE *f = NULL;
  if (proc == -1) {
    f = fopen("/proc/self/stat", "r");
  } else {
    // On 64-bit systems, pid_max can be set to any value up to 2^22
    // (PID_MAX_LIMIT, approximately 4 million).
    char pids[PATH_MAX];
    snprintf(pids, sizeof pids, "/proc/%u/stat", proc);
    f = fopen(pids, "r");
  }
  if (f) {
    fscanf(f, "%d "
              "%s %c "
              "%d %d %d %d %d "
              "%u "
              "%lu %lu %lu %lu %lu %lu "
              "%ld %ld %ld %ld %ld %ld "
              "%llu "
              "%lu "
              "%ld "
              "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
              "%d %d %u %u",
           &pid,
           cmd, &state,
           &ppid, &pgrp, &session, &tty_nr, &tpgid,
           &flags,
           &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
           &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue,
           &starttime,
           &vsize,
           &rss,
           &rsslim, &startcode, &endcode, &startstack, &kstkesp, &kstkeip,
           &signal_map, &blocked, &sigignore, &sigcatch, &wchan, &nswap,
           &cnswap,
           &exit_signal, &processor,
           &rt_priority, &policy);
  }
  fclose(f);
  *start      = startcode;
  *end        = endcode;
  *stackstart = startstack;
}

static char**
copyArgv(int argc, char **argv)
{
  char **new_argv = malloc((argc+1) * sizeof *new_argv);
  for(int i = 0; i < argc; ++i)
  {
      size_t length = strlen(argv[i])+1;
      new_argv[i] = malloc(length);
      memcpy(new_argv[i], argv[i], length);
  }
  new_argv[argc] = NULL;
  return new_argv;
}

static int
isValidFd(int fd)
{
  return fcntl(fd, F_GETFL, 0) != -1;
}

// Global functions

void
updateEnviron(const char **newenviron)
{
  __environ = (char **)newenviron;
}

// MPI Spec: A call to MPI_INIT has the same effect as a call to
// MPI_INIT_THREAD with a required = MPI_THREAD_SINGLE.
int
getRank(int init_flag)
{
  int flag;
  int world_rank = -1;
  int retval = MPI_SUCCESS;
  int provided;

  MPI_Initialized(&flag);
  if (!flag) {
    if (init_flag == MPI_INIT_NO_THREAD) {
      retval = MPI_Init(NULL, NULL);
    }
    else {
      retval = MPI_Init_thread(NULL, NULL, init_flag, &provided);
    }
  }
  if (retval == MPI_SUCCESS) {
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  }
  return world_rank;
}

// Lower-half's linkmap points to current vDSO and that needs to be updated
// with the parent's (upper-half) vDSO after doing copy-the-bits
void *
getVdsoPointerInLinkMap()
{
  struct link_map *map;
  void *lh_handle;
  lh_handle = dlopen(NULL, RTLD_NOW);
  assert(lh_handle != NULL);
  int ret = dlinfo(lh_handle, RTLD_DI_LINKMAP, (void **)&map);
  assert(ret == 0);
  while (map) {
    if (strstr(map->l_name, "vdso") != NULL) {
      return &(map->l_ld);
    }
    map = map->l_next;
  }
  return NULL;
}


// This does nothing, unless modified.  See mpi-wrappers/mpi_wrappers.cpp for
// its definition, and how to use it for debugging lower half during restart.
int MPI_MANA_Internal(char *dummy) {
  return 0;
}

#ifdef SINGLE_CART_REORDER
// Prior to checkpoint we will use the normal variable names, and
// after restart we will use the '_prime' suffix with variable names.
MPI_Comm comm_cart_prime;

int
getCoordinates(CartesianProperties *cp, int *coords, int init_flag)
{
  int flag;
  int comm_old_rank = -1;
  int comm_cart_rank = -1;
  int retval = MPI_SUCCESS;
  int provided;

  MPI_Initialized(&flag);
  if (!flag) {
    if (init_flag == MPI_INIT_NO_THREAD) {
      retval = MPI_Init(NULL, NULL);
    }
    else {
      retval = MPI_Init_thread(NULL, NULL, init_flag, &provided);
    }
  }
  if (retval == MPI_SUCCESS) {
    MPI_Cart_create(MPI_COMM_WORLD, cp->ndims, cp->dimensions, cp->periods,
                    cp->reorder, &comm_cart_prime);
    MPI_Comm_rank(comm_cart_prime, &comm_cart_rank);
    MPI_Cart_coords(comm_cart_prime, comm_cart_rank, cp->ndims, coords);
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_old_rank);
  }
  return comm_old_rank;
}

void
getCartesianCommunicator(MPI_Comm **comm_cart)
{
  *comm_cart = &comm_cart_prime;
}
#endif

void*
mydlsym(enum MPI_Fncs fnc)
{
  if (fnc < MPI_Fnc_NULL || fnc > MPI_Fnc_Invalid) {
    return NULL;
  }
  return MPI_Fnc_Ptrs[fnc];
}

__attribute__((constructor))
void first_constructor()
{
  static int firstTime = 1;

  if (firstTime) {
    DLOG(VERBOSE, "(1) Constructor: We'll pass information to the parent.\n");
    firstTime = 0;

    // Pre-initialize this component of lh_info.
    // mtcp_restart analyzes the memory layout, and then writes this to us.
    // lh_memRange is the memory range to be used for any mmap's by lower half.
    read(0, &lh_memRange, sizeof(lh_memRange));

    unsigned long start, end, stackstart;
    unsigned long pstart, pend, pstackstart;
    unsigned long fsaddr = 0;
    Area txt, data, heap;
    getTextSegmentRange(getpid(), &start, &end, &stackstart);
    getTextSegmentRange(getppid(), &pstart, &pend, &pstackstart);
    syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
    start = ROUND_UP(start);
    end   = ROUND_UP(end);
    txt.addr = (VA)start;
    txt.endAddr = (VA)end;
    getDataFromMaps(&txt, &heap);

    // TODO: Verify that this gives us the right value every time
    // Perhaps use proc maps in the future?
    int argc = *(int*)stackstart;
    char **argv = (char**)(stackstart + sizeof(unsigned long));

    lh_info.startText = (void*)start;
    lh_info.endText = (void*)end;
    lh_info.endOfHeap = (void*)heap.endAddr;
    lh_info.libc_start_main = &__libc_start_main;
    lh_info.main = &main;
    lh_info.libc_csu_init = &__libc_csu_init;
    lh_info.libc_csu_fini = &__libc_csu_fini;
    lh_info.fsaddr = (void*)fsaddr;
    lh_info.lh_AT_PHNUM = getauxval(AT_PHNUM);
    lh_info.lh_AT_PHDR = getauxval(AT_PHDR);
    lh_info.g_appContext = (void*)&g_appContext;
    lh_info.lh_dlsym = (void*)&mydlsym;
    lh_info.getRankFptr = (void*)&getRank;

#ifdef SINGLE_CART_REORDER
    lh_info.getCoordinatesFptr = (void*)&getCoordinates;
    lh_info.getCartesianCommunicatorFptr = (void *)&getCartesianCommunicator;
#endif

    lh_info.parentStackStart = (void*)pstackstart;
    lh_info.updateEnvironFptr = (void*)&updateEnviron;
    lh_info.getMmappedListFptr = (void*)&getMmappedList;
    lh_info.resetMmappedListFptr = (void*)&resetMmappedList;
    lh_info.memRange = lh_memRange;
    lh_info.numCoreRegions = totalRegions;
    lh_info.lh_regions_list = &lh_core_regions;
    lh_info.getLhRegionsListFptr = (void*)&getLhRegionsList;
    lh_info.vdsoLdAddrInLinkMap = getVdsoPointerInLinkMap();
    DLOG(INFO, "startText: %p, endText: %p, endOfHeap; %p\n",
         lh_info.startText, lh_info.endText, lh_info.endOfHeap);
    // Write LH core regions list to stdout, for the parent process to read.
    if (write(1, &totalRegions, sizeof(totalRegions)) != sizeof(totalRegions)) {
      fprintf(stderr, "* WARNING: totalRegions (numCoreRegions) not sent\n");
      fflush(stderr);
    }
    size_t total_bytes = totalRegions*sizeof(LhCoreRegions_t);
    if (write(1, &lh_core_regions, total_bytes) < total_bytes) {
      fprintf(stderr, "* WARNING: lh_proxy didn't write all lh_core_regions\n");
      fflush(stderr);
    }
    // Write the addr of the lower-half lh_info struct in the stdout.
    // The parent process will: (i) read this pipe; (ii) store lh_info_addr
    // in the upper half; and (iii) then copy the address space of this child
    // process into the lower half in the parent process using process_vm_readv.
    // NOTE:  All of this happens in mpi-proxy-split/split_process.cpp
    // and in restart_plugin/mtcp_split_process.c (during launch and restart).
    LowerHalfInfo_t *lh_info_addr = &lh_info;
    if (write(1, &lh_info_addr, sizeof(lh_info_addr)) != sizeof(lh_info_addr)) {
      fprintf(stderr, "* WARNING: lh_info_addr not sent\n");
      fflush(stderr);
    }

#if 0
    // Send sizeof(lh_info) as consistency ehck.
    // Will it sizeolh_info->pluginInfo) == restart_plugi/mtcp_split_process.c?
    int sizeof_lh_info = sizeof(lh_info);
    write(1, &sizeof_lh_info, sizeof(lh_info));
    // Write lh_info to stdout, for mtcp_split_process.c to read.
    write(1, &lh_info, sizeof lh_info);
    // It's okay to have an infinite loop here.  Our parent has promised to
    // kill us after it copies our bits.  So, this child doesn't need to exit.
#endif
    while(1);
  } else {
    DLOG(VERBOSE, "(2) Constructor: Running in the parent?\n");
  }
}

__attribute__((destructor))
void second_destructor()
{
  // Destructor: The application called exit in the destructor to
  // get here. After this, we call setcontext() to get back in the
  // application.
  DLOG(VERBOSE, "Destructor!\n");
}
