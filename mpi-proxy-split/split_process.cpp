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

// Needed for process_vm_readv
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <linux/version.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <libgen.h>
#include <limits.h>
#include <link.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>


#include "jassert.h"
#include "libproxy.h"
#include "split_process.h"
#include "procmapsutils.h"
#include "config.h" // for HAS_FSGSBASE
#include "constants.h"
#include "util.h"
#include "dmtcp.h"

static unsigned long origPhnum;
static unsigned long origPhdr;
LowerHalfInfo_t lh_info;
LowerHalfInfo_t *lh_info_addr;
proxyDlsym_t pdlsym; // initialized to (proxyDlsym_t)lh_info.lh_dlsym
LhCoreRegions_t lh_regions_list[MAX_LH_REGIONS] = {0};

static unsigned long getStackPtr();
static void patchAuxv(ElfW(auxv_t) *, unsigned long , unsigned long , int );
static int mmap_iov(const struct iovec *, int );
static int read_lh_proxy_bits(pid_t );
static pid_t startProxy();
static int initializeLowerHalf();
static MemRange_t setLhMemRange();

#if 0
// TODO: Code to compile the lower half at runtime to adjust for differences
// in memory layout in different environments.
static Area getHighestAreaInMaps();
static char* proxyAddrInApp();
static void compileProxy();
#endif

bool FsGsBaseEnabled = false;

bool CheckAndEnableFsGsBase()
{
  const char *str = getenv(ENV_VAR_FSGSBASE_ENABLED);
  if (str != NULL && str[0] == '1') {
    FsGsBaseEnabled = true;
  }

  return FsGsBaseEnabled;
}

SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
  this->lowerHalfFs = lowerHalfFs;
  this->upperHalfFs = getFS();
  setFS(this->lowerHalfFs);
}

SwitchContext::~SwitchContext()
{
  setFS(this->upperHalfFs);
}

int
splitProcess()
{
#if 0
  compileProxy();
#endif
  JTRACE("Initializing Proxy");
  pid_t childpid = startProxy();
  int ret = -1;
  if (childpid > 0) {
    ret = read_lh_proxy_bits(childpid);
    // FIXME: Replace lh_info with lh_info_addr everywhere
    // And remove lh_info from .h
    memcpy(&lh_info, lh_info_addr, sizeof(lh_info)); // Populate lh_info now
    kill(childpid, SIGKILL);
    waitpid(childpid, NULL, 0);
  }
  if (ret == 0) {
    ret = initializeLowerHalf();
  }
  return ret;
}

// FIXME: This code is duplicated in lh_proxy and plugin. Refactor into utils.
// Returns the address of argc on the stack
static unsigned long
getStackPtr()
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

  FILE* f = fopen("/proc/self/stat", "r");
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
    fclose(f);
  }
  return startstack;
}

// Sets the given AT_PHNUM and AT_PHDR fields of the given auxiliary vector
// pointer, 'av', to the values specified by 'phnum' and 'phdr', respectively.
// If 'save' is 1, the original values of the two fields are saved to global
// global members. If 'save' is 0, the original values of the two fields are
// restored to the earlier saved values and the given values are ignored.
static void
patchAuxv(ElfW(auxv_t) *av, unsigned long phnum, unsigned long phdr, int save)
{
  for (; av->a_type != AT_NULL; ++av) {
    switch (av->a_type) {
      case AT_PHNUM:
        if (save) {
          origPhnum = av->a_un.a_val;
          av->a_un.a_val = phnum;
        } else {
          av->a_un.a_val = origPhnum;
        }
        break;
      case AT_PHDR:
        if (save) {
         origPhdr = av->a_un.a_val;
         av->a_un.a_val = phdr;
        } else {
          av->a_un.a_val = origPhdr;
        }
        break;
      default:
        break;
    }
  }
}

// Mmaps a memory region at address and length specified by the given IO vector,
// 'iov', and with the given protections, 'prot'.
static int
mmap_iov(const struct iovec *iov, int prot)
{
  void *base = (void *)ROUND_DOWN(iov->iov_base, PAGE_SIZE);
  size_t len = ROUND_UP(iov->iov_len, PAGE_SIZE);
  int flags =  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
  // Get fd for "lh_proxy", for use in mmap().  Kernel will then
  //   display the lh_proxy pathname in /proc/PID/maps.
  dmtcp::string progname = dmtcp::Util::getPath("lh_proxy");
  int imagefd = open(progname.c_str(), O_RDONLY);
  JASSERT(imagefd != -1)
	 (JASSERT_ERRNO)(progname.c_str()).Text("Failed to open file");
  flags ^= MAP_ANONYMOUS;
  void *ret = mmap(base, len, prot, flags, imagefd, 0);
  JWARNING(ret != MAP_FAILED)
          (JASSERT_ERRNO)(base)(len)(prot).Text("Error mmaping memory");
  close(imagefd);
  return ret == MAP_FAILED;
}

// Copies over the lower half memory segments from the child process with the
// given pid, 'childpid'. On success, returns 0. Otherwise, -1 is returned.
static int
read_lh_proxy_bits(pid_t childpid)
{
  int ret = -1;
  const int IOV_SZ = lh_info.numCoreRegions;
  struct iovec remote_iov[IOV_SZ];
  for (int i = 0; i < IOV_SZ; i++) {
  // get the metadata and map the memory segment
    remote_iov[i].iov_base = lh_regions_list[i].start_addr;
    remote_iov[i].iov_len = (unsigned long)lh_regions_list[i].end_addr -
                            (unsigned long)lh_regions_list[i].start_addr;
    ret = mmap_iov(&remote_iov[i], lh_regions_list[i].prot | PROT_WRITE);
    JWARNING(ret == 0)(remote_iov[i].iov_base)(remote_iov[i].iov_len)
          .Text("Error mapping lh_proxy memory segment");

    // NOTE: For the process_vm_readv call, the local_iov will be same as
    //       remote_iov, since we are just duplicating child processes memory.
    // NOTE: This requires same privilege as ptrace_attach (valid for child).
    //       Anecdotally, in containers, we've seen a case where this errors out
    //       with ESRCH (no such proc.); it may need CAP_SYS_PTRACE privilege??
    //       See, for example: https://github.com/getsentry/sentry-native/issues/578
    //            for interaction with Docker.
    //       man ptrace:
    //          PTRACE_MODE_ATTACH_REALCREDS satisfied by PTRACE_MODE_REALCREDS
    //       man ptrace: PTRACE_MODE_REALCREDS was the default before Linux 4.5.
    JTRACE("Reading segment from lh_proxy")
          (remote_iov[i].iov_base)(remote_iov[i].iov_len);
    ret = process_vm_readv(childpid, remote_iov + i, 1, remote_iov + i, 1, 0);
    JASSERT(ret != -1)(JASSERT_ERRNO).Text("Error reading data from lh_proxy");
    // Can remove PROT_WRITE now that we've populated the segment.
    // Update: on Perlmutter, we need to keep the PROT_WRITE permission as
    // libc writes to a read-only region for some reason. Keeping PROT_WRITE is
    // harmless.
    // ret = mprotect(remote_iov[i].iov_base, remote_iov[i].iov_len,
    //              lh_regions_list[i].prot | PROT_WRITE);
  }
  return 0;
}

// gethostbyname_proxy is found in DMTCP bin directory.  Add to PATH.
// It will be needed by the statically linked lower half after it is
//   copied into the actual split process and MPI_Init() is called.
// See ./lower-half/gethostbyname-static for details.
void addPathFor_gethostbyname_proxy() {
  // Use path of lh_proxy, to avoid any system-wide gethostbyname_proxy.
  dmtcp::string progname = dmtcp::Util::getPath("dmtcp_launch");
  dmtcp::string newPath = dirname((char *)progname.c_str());
  newPath = newPath + ":" + getenv("PATH");
  int rc = setenv("PATH", newPath.c_str(), 1); // This copies the string.
  if (rc == -1) { perror("setenv"); exit(2); }
}

// Starts a child process for the lower half application, and returns the PID
// of the child process. Also populates the global 'lh_info' object with the
// LowerHalfInfo_t data from the child process.
static pid_t
startProxy()
{
  int pipefd_in[2] = {0};
  int pipefd_out[2] = {0};

  if (pipe(pipefd_in) < 0) {
    JWARNING(false)(JASSERT_ERRNO).Text("Failed to create pipe");
    return -1;
  }
  if (pipe(pipefd_out) < 0) {
    JWARNING(false)(JASSERT_ERRNO).Text("Failed to create pipe");
    return -1;
  }

  int childpid = fork();
  switch (childpid) {
    case -1:
      JWARNING(false)(JASSERT_ERRNO).Text("Failed to fork lh_proxy");
      break;

    case 0:
    {
      dup2(pipefd_out[1], 1); // Will write lh_info to stdout.
      close(pipefd_out[1]);
      close(pipefd_out[0]); // Close reading end of pipe.

      dmtcp::string nockpt = dmtcp::Util::getPath("dmtcp_nocheckpoint");
      dmtcp::string progname = dmtcp::Util::getPath("lh_proxy");
      if (getenv("USE_LH_PROXY_DEFADDR")) {
        progname = progname + "_da";
      }
      char* const args[] = {const_cast<char*>(nockpt.c_str()),
                            const_cast<char*>(progname.c_str()),
                            NULL};

      // Move reading end of pipe to stadin of lh_proxy.
      // Can then write pipefd_out[1] to lh_proxy.
      //   (But that would be better if lh_proxy simply wrote to stdout.)
      // Then, lh_proxy can read lh_mem_range (type: MemRange_t).
      dup2(pipefd_in[0], 0);
      close(pipefd_in[0]);

      personality(ADDR_NO_RANDOMIZE);
      // This will call:  lower-half/libproxy.c/first_constructor
      // (This global constructor executes before main in lh_proxy.)
      JASSERT(execvp(args[0], args) != -1)(JASSERT_ERRNO)
        .Text("Failed to exec lh_proxy");
      break;
    }

    default: // in parent
    {
      // Write to stdin of lh_proxy the memory range for mmap's by lower half
      MemRange_t mem_range = setLhMemRange();
      write(pipefd_in[1], &mem_range, sizeof(mem_range));
      close(pipefd_in[1]); // close writing end of pipe
      // Read from stdout of lh_proxy full lh_info struct, including orig memRange.
      close(pipefd_out[1]); // close write end of pipe

      // Read info on lh_core_regions
      int num_lh_core_regions;
      int ret = dmtcp::Util::readAll(pipefd_out[0],
                          &num_lh_core_regions, sizeof(num_lh_core_regions));
      JASSERT(num_lh_core_regions <= MAX_LH_REGIONS) (num_lh_core_regions)
        .Text("Invalid number of LH core regions");
      size_t total_bytes = num_lh_core_regions*sizeof(LhCoreRegions_t);
      ret = dmtcp::Util::readAll(pipefd_out[0], &lh_regions_list, total_bytes);
      JWARNING(ret == (ssize_t)total_bytes) (ret) (total_bytes) (JASSERT_ERRNO)
                .Text("Read fewer bytes than expected for LH core regions");
      ret = dmtcp::Util::readAll(pipefd_out[0],
                                 &lh_info_addr, sizeof(lh_info_addr));

      // FIXME:  We should use num_lh_core_regions to read lh core regions,
      //         instead of passing it through lh_info.numCoreRegions.
      //         Declare num_lh_core_regions next to lh_regions_list[].
      lh_info.numCoreRegions = num_lh_core_regions;

      close(pipefd_out[0]);
      addPathFor_gethostbyname_proxy(); // used by statically linked lower half
    }
  }
  return childpid;
}

// This sets the address range of the lower half dynamically by searching
// through the memory region for the first available free region.
// FIXME:  restart_plugin/mtcp_split_process.c uses MANA_USE_LH_FIXED_ADDRESS.
//         Consider testing and using it here, also.
static void
findLHMemRange(MemRange_t *lh_mem_range)
{
  bool is_set = false;

  Area area;
  char prev_path_name[PATH_MAX];
  char next_path_name[PATH_MAX];
  uint64_t prev_addr_end;
  uint64_t next_addr_start;
  uint64_t next_addr_end;

  int mapsfd = open("/proc/self/maps", O_RDONLY);
  JASSERT(mapsfd >= 0)(JASSERT_ERRNO).Text("Failed to open proc maps");

  if (readMapsLine(mapsfd, &area)) {
    // ROUND_UP aligns the address such that HUGEPAGES/2MB can also be used.
    prev_addr_end = ROUND_UP((uint64_t) area.endAddr, 2 * ONEMB);
  }

  while (readMapsLine(mapsfd, &area)) {
    next_addr_start = (uint64_t) area.addr;
    next_addr_end = ROUND_UP((uint64_t) area.endAddr, 2 * ONEMB);
    strncpy(next_path_name, area.name, PATH_MAX - 1);
    next_path_name[PATH_MAX - 1] = '\0';

    // We add a 1GB buffer between the end of the heap or the start of the
    // stack.
    if (strcmp(next_path_name, "[heap]") == 0) {
      next_addr_end += ONEGB;
    } else if (strcmp(next_path_name, "[stack]") == 0) {
      next_addr_start -= ONEGB;
    }

    // Normally, the first 2GB of space will be available between the
    // upper half's data section and its libraries. We should leave space for
    // the upper half's heap and the lower half's core regions which are yet to
    // be loaded. So, it is better to reserve 2GB space for the lower half mmaps
    // near the next address instead of the previous address with a one-page
    // distance to avoid memory overlap.
    // FIXME:  Changed 2GB to 8GB for larger region between lower and upper
    //         half.  We should allocate 10 MB above lower half, with CANARY
    //         and PROT_NONE, and test if the pages were disturbed in mmap().
    if ((prev_addr_end + 8 * ONEGB + PAGE_SIZE) <= next_addr_start) {
      lh_mem_range->start = (VA)next_addr_start - (8 * ONEGB + PAGE_SIZE) ;
      lh_mem_range->end = (VA)next_addr_start - PAGE_SIZE;
      is_set = true;
      break;
    }
    prev_addr_end = next_addr_end;
  }
  close(mapsfd);

  JASSERT(is_set)(JASSERT_ERRNO)
    .Text("No memory region can be found for the lower half");
}

// Sets the address range for the lower half, dynamically determined by the
// above function. All memory allocations done by the lower half are restricted
// to the specified address range.
static MemRange_t
setLhMemRange()
{
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  JASSERT(mapsfd >= 0)(JASSERT_ERRNO).Text("Failed to open proc maps");
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]")) {
      found = true;
      break;
    }
  }
  close(mapsfd);
  static MemRange_t lh_mem_range;
  if (found) {
    findLHMemRange(&lh_mem_range);
  } else {
    JASSERT(false).Text("Failed to find [stack] memory segment\n");
  }
  return lh_mem_range;
}

void *
getUhVdsoLdAddr()
{
  struct link_map *map;
  void *uh_handle;
  uh_handle = dlopen(NULL, RTLD_NOW);
  JASSERT(uh_handle != NULL);
  int ret = dlinfo(uh_handle, RTLD_DI_LINKMAP, (void **)&map);
  JASSERT(ret == 0);
  while (map) {
    if (strstr(map->l_name, "vdso") != NULL) {
      return map->l_ld;
    }
    map = map->l_next;
  }
  return NULL;
}

void
updateVdsoLinkmapEntry(void * l_ld_addr)
{
  if (l_ld_addr == NULL) {
    // The lower-half link map did not have a link to "vdso".
    // So, we can skip patching the lower-half link map.
    return;
  }
  void *uh_l_ld_vdso = getUhVdsoLdAddr();
  // now update it in the lower-half's linkmap
  *(uint64_t *)l_ld_addr = (uint64_t)uh_l_ld_vdso;
  // FIXME: dynamically find the address in lh_info instead of ad-hoc 0xa0
  *(uint64_t *)((uint64_t)l_ld_addr + 0xa0) = (uint64_t)uh_l_ld_vdso;
}

// Initializes the libraries (libc, libmpi, etc.) of the lower half
// FIXME: with optimization, lower half initialization segfaults on CentOS
NO_OPTIMIZE
static int
initializeLowerHalf()
{
  int ret = 0;
  bool lh_initialized = false;
  unsigned long argcAddr = getStackPtr();
  // NOTE: proc-stat returns the address of argc on the stack.
  // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
  // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)

  void *stack_end = (void*)(argcAddr - sizeof(unsigned long));
  int argc = *(int*)argcAddr;
  char **argv = (char**)(argcAddr + sizeof(unsigned long));
  char **ev = &argv[argc + 1];
  // char **ev = &((unsigned long*)stack_end[argc + 1]);
  libcFptr_t fnc = (libcFptr_t)lh_info.libc_start_main;

  // Save the pointer to mydlsym() function in the lower half. This will be
  // used in all the mpi-wrappers.
  pdlsym = (proxyDlsym_t)lh_info.lh_dlsym;

  // Copied from glibc source
  ElfW(auxv_t) *auxvec;
  {
    char **evp = ev;
    while (*evp++ != NULL);
    auxvec = (ElfW(auxv_t) *) evp;
  }
  // update vDSO linkmap entry
  updateVdsoLinkmapEntry(lh_info.vdsoLdAddrInLinkMap);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  // Clear any saved mappings in the lower half
  resetMmappedList_t resetMaps =
    (resetMmappedList_t)lh_info.resetMmappedListFptr;
  resetMaps();
  // Set the auxiliary vector to correspond to the values of the lower half
  // (which is statically linked, unlike the upper half). Without this, glibc
  // will get confused during the initialization.
  patchAuxv(auxvec, lh_info.lh_AT_PHNUM, lh_info.lh_AT_PHDR, 1);
  // Save upper half's ucontext_t in a global object in the lower half, as
  // specified by the lh_info.g_appContext pointer
  JWARNING(getcontext((ucontext_t*)lh_info.g_appContext) == 0)(JASSERT_ERRNO);

  if (!lh_initialized) {
    // Initialize the lower half by calling the __libc_start_main function
    // in the lower half, if not already done.
    lh_initialized = true;
    fnc((mainFptr)lh_info.main, argc, argv,
        (mainFptr)lh_info.libc_csu_init,
        (finiFptr)lh_info.libc_csu_fini, 0, stack_end);
  }
  JTRACE("After getcontext");
  // Restore the the auxiliary vector to correspond to the values of the upper
  // half.
  patchAuxv(auxvec, 0, 0, 0);
  RETURN_TO_UPPER_HALF();
  return ret;
}

#if 0
static Area
getHighestAreaInMaps()
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  Area highest_area;
  mtcp_readMapsLine(mapsfd, &highest_area);
  while (mtcp_readMapsLine(mapsfd, &area)) {
    if (area.endAddr > highest_area.endAddr) {
      highest_area = area;
    }
  }
  close(mapsfd);
  return highest_area;
}

static char*
proxyAddrInApp()
{
  Area highest_area = getHighestAreaInMaps();
  // two pages after after highest memory section?
  return highest_area.endAddr + (2*getpagesize());
}

static void
compileProxy()
{
  dmtcp::string cmd = "make lh_proxy PROXY_TXT_ADDR=0x";
  dmtcp::string safe_addr = proxyAddrInApp();
  cmd.append(safe_addr);
  int ret = system(cmd.c_str());
  if (ret == -1) {
      ret = system("make lh_proxy");
      if (ret == -1) {
        JWARNING(false)(JASSERT_ERRNO).Text("Proxy building failed!");
      }
  }
}
#endif
