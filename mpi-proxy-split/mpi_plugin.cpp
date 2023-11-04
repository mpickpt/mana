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

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef SINGLE_CART_REORDER
#include "cartesian.h"
#endif

#include <cxxabi.h>  /* For backtrace() */
#include <execinfo.h>  /* For backtrace() */

#include <regex>

#include "mpi_files.h"
#include "mana_header.h"
#include "mpi_plugin.h"
#include "lower_half_api.h"
#include "split_process.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "record-replay.h"
#include "seq_num.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"

#include "config.h"
#include "dmtcp.h"
#include "kvdb.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "procselfmaps.h"

extern "C" int MPI_MANA_Internal(char *dummy);

using namespace dmtcp;
using dmtcp::kvdb::KVDBRequest;
using dmtcp::kvdb::KVDBResponse;

/* Global variables */
#ifdef SINGLE_CART_REORDER
extern CartesianProperties g_cartesian_properties;
#endif

extern ManaHeader g_mana_header;
extern std::unordered_map<MPI_File, OpenFileParameters> g_params_map;

constexpr const char *MANA_FILE_REGEX_ENV = "MANA_FILE_REGEX";
constexpr const char *MANA_SEGV_DEBUG_LOOP = "MANA_SEGV_DEBUG_LOOP";
static std::regex *file_regex = NULL;
static map<string, int> *g_file_flags_map = NULL;
static vector<int> ckpt_fds;
static bool processingOpenCkpFileFds = false;

std::unordered_map<pid_t, unsigned long> *g_upper_half_fsbase = NULL;
DmtcpMutex g_upper_half_fsbase_lock;
bool CheckAndEnableFsGsBase();

EXTERNC pid_t dmtcp_get_real_tid() __attribute((weak));

mana_state_t mana_state = UNKNOWN_STATE;

ProcSelfMaps *preMpiInitMaps = nullptr;
ProcSelfMaps *postMpiInitMaps = nullptr;
map<void*, size_t> *mpiInitLhAreas = nullptr;
void *heapAddr = nullptr;

#undef dmtcp_skip_memory_region_ckpting
// High memory could start at 0x7ffc00000000 on Perlmutter
const VA HIGH_ADDR_START = (VA)0x7ffc00000000;

static bool isLhDevice(const ProcMapsArea *area);
static bool isLhCoreRegion(const ProcMapsArea *area);
static bool isLhMmapRegion(const ProcMapsArea *area);
// FIXME:  isLhMpiInitRegion operates only in a 'launch' process, not 'restart'
static bool isLhMpiInitRegion(const ProcMapsArea *area);
static bool isLhRegion(const ProcMapsArea *area);

void *old_brk;

// Check if haystack region contains needle region.
static inline int
regionContains(const void *haystackStart,
               const void *haystackEnd,
               const void *needleStart,
               const void *needleEnd)
{
  return needleStart >= haystackStart && needleEnd <= haystackEnd;
}

// These are called from the MPI_Init wrapper in mpi-wrappers/mpi_wrappers.cpp
// FIXME: But during restart, MPI_Init is called from getRankFptr (aka getRank),
//        which calls MPI_Init before MPI_Comm_rank.  If this is a
//        restarted process (not a 'launch' process), then these functions
//        are not called, and we miss recording the ioctl of /anon_hugepage .
//          To fix this, restart_plugin needs to call this after getRankFptr,
//        and simply record all memory regions as part of the lower half.
//        Note that restart_plugin is part of mtcp_restart (no libc available).
void recordPreMpiInitMaps()
{
  if (preMpiInitMaps != nullptr) {
    return;
  }
  mpiInitLhAreas = new map<void*, size_t>();
  preMpiInitMaps = new ProcSelfMaps();
}

void recordPostMpiInitMaps()
{
  if (postMpiInitMaps != nullptr) {
    return;
  }
  postMpiInitMaps = new ProcSelfMaps();
  ProcMapsArea area;

  {
    ostringstream o;
    const void *procSelfMapsData = postMpiInitMaps->getData();

    while (postMpiInitMaps->getNextArea(&area)) {
      if (!regionContains(area.addr, area.endAddr,
                          procSelfMapsData, procSelfMapsData)) {
        mpiInitLhAreas->insert(std::make_pair(area.addr, area.size));;
      }
    }

    // Now remove those mappings that existed before Mpi_Init.
    JASSERT(preMpiInitMaps != nullptr);
    while (preMpiInitMaps->getNextArea(&area)) {
      if (mpiInitLhAreas->find(area.addr) != mpiInitLhAreas->end()) {
        JWARNING(mpiInitLhAreas->at(area.addr) == area.size)((void *)area.addr)
                (area.size);
        mpiInitLhAreas->erase(area.addr);
      } else {
        // Check if a region has the same end addr. E.g., thread stack grew.
        for (auto it : *mpiInitLhAreas) {
          void *endAddr = (char*) it.first + it.second;
          if (endAddr == area.endAddr) {
            mpiInitLhAreas->erase(area.addr);
            break;
          }
        }
      }
    }
  }
}

void recordMpiInitMaps()
{
  string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));
  //kvdb::set(workerPath, "ProcSelfMaps_PreMpiInit", preMpiInitMaps->getData());
  //kvdb::set(workerPath, "ProcSelfMaps_PostMpiInit",
  //          postMpiInitMaps->getData());

  ProcMapsArea area;
  ProcSelfMaps maps;
  if (mpiInitLhAreas->size() > 0)
  {
    ostringstream o;
    while (maps.getNextArea(&area)) {
      if (mpiInitLhAreas->find(area.addr) != mpiInitLhAreas->end()) {
        o << std::hex << (uint64_t) area.addr << "-" << (uint64_t) area.endAddr;
        if (area.name[0] != '\0') {
          o << "        " << area.name;
        }
        o << "\n";
      }
    }

    kvdb::set(workerPath, "ProcSelfMaps_LhCoreRegionsMpiInit", o.str());
  }

  if (lh_info.numCoreRegions > 0) {
    ostringstream o;
    for (int i = 0; i < lh_info.numCoreRegions; i++) {
      o << std::hex << (uint64_t) lh_regions_list[i].start_addr << "-"
        << (uint64_t) lh_regions_list[i].end_addr << "\n";
    }

    kvdb::set(workerPath, "ProcSelfMaps_LhCoreRegions", o.str());
  }
}

void recordOpenFds()
{
  ostringstream o;
  vector<int>fds = jalib::Filesystem::ListOpenFds();
  for (int fd : fds) {
    if (!Util::isValidFd(fd)) {
      continue;
    }

    string device = jalib::Filesystem::GetDeviceName(fd);
    o << fd << " -> " << device << "\n";
  }

  string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));
  kvdb::set(workerPath, "ProcSelfFds", o.str());
}

static void
processFileOpen(const char *path, int flags)
{
  if (file_regex == NULL ||
      processingOpenCkpFileFds ||
      Util::strStartsWith(path, "/proc") ||
      Util::strStartsWith(path, "/sys")) {
    return;
  }

  if ((flags & O_APPEND) ||
      ((flags & O_RDWR) || (flags & O_WRONLY))) {
    if (std::regex_search(path, *file_regex)) {
      g_file_flags_map->insert(std::make_pair(path, flags));
    }
  }
}

static void
openCkptFileFds()
{
  ostringstream o;
  ostringstream staleFilesStr;

  processingOpenCkpFileFds = true;
  ckpt_fds.clear();
  ckpt_fds.reserve(g_file_flags_map->size());

  vector<string> staleFiles;

  for (auto i : *g_file_flags_map) {
    string const& path = i.first;
    off64_t flags = i.second;

    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
      int fd = open(path.c_str(), O_APPEND | O_WRONLY);
      JWARNING(fd != -1)(path)(flags)(JASSERT_ERRNO);

      if (fd != -1) {
        o << path << ";size:" << statbuf.st_size << ";flags:" << std::oct << flags << std::dec << "\n";
        ckpt_fds.push_back(fd);
      }
    } else {
      staleFilesStr << path << ":" << flags<< "\n";
      staleFiles.push_back(path);
    }
  }

  for (string const& stale : staleFiles) {
    g_file_flags_map->erase(stale);
  }

  string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));
  kvdb::set(workerPath, "Ckpt_Files", o.str());
  kvdb::set(workerPath, "Ckpt_Files_Stale", staleFilesStr.str());
}

static void
closeCkptFileFds()
{
  for (int fd : ckpt_fds) {
    close(fd);
  }

  ckpt_fds.clear();
}

static void
logCkptFileFds()
{
  ostringstream o;
  struct stat statbuf;
  string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));

  for (int fd : ckpt_fds) {
    JASSERT(fstat(fd, &statbuf) == 0);
    o << jalib::Filesystem::GetDeviceName(fd) << ";size:" << statbuf.st_size << "\n";
  }

  if (ckpt_fds.size() > 0) {
    kvdb::set(workerPath, "Ckpt_Files_Restart", o.str());
  }
}

static bool
isLhDevice(const ProcMapsArea *area)
{
  // FIXME:  An MPI application might also create /dev/zero in upper half.
  if (strstr(area->name, "/dev/zero") ||
      strstr(area->name, "/dev/kgni") ||
      /* DMTCP SysVIPC plugin should be able to C/R this correctly
       * And if it's created by lower half, we hope it goes through mmap,
       * and so it will be recognized by isLhMmapRegion().
       */
      // strstr(area->name, "/SYSV") ||
      strstr(area->name, "/dev/xpmem") ||
      // strstr(area->name, "xpmem") ||
      // strstr(area->name, "hugetlbfs" /* Obsolete:  From Cori */) ||
      strstr(area->name, "/dev/shm")) {
    return true;
  }

  return false;
}

static bool
isLhCoreRegion(const ProcMapsArea *area)
{
  for (int i = 0; i < lh_info_addr->numCoreRegions; i++) {
    void *lhStartAddr = lh_info_addr->lh_regions_list[i].start_addr;
    void *lhEndAddr = lh_info_addr->lh_regions_list[i].end_addr;
    if (area->addr >= lhStartAddr && area->addr < lhEndAddr) {
      JTRACE ("Ignoring LH core region") ((void*)area->addr);
      return 1;
    }
  }
  return false;
}

static bool
isLhMmapRegion(const ProcMapsArea *area)
{
  return regionContains(lh_info.memRange.start, lh_info.memRange.end,
                        area->addr, area->endAddr);
}

// FIXME:  isLhMpiInitRegion operates only in a 'launch' process, not 'restart'
static bool
isLhMpiInitRegion(const ProcMapsArea *area)
{
  if (mpiInitLhAreas != NULL &&
      mpiInitLhAreas->find(area->addr) != mpiInitLhAreas->end()) {
    JTRACE("Ignoring MpiInit region")(area->name)((void*)area->addr);
    return true;
  }

  return false;
}

static bool
isLhRegion(const ProcMapsArea *area)
{
  if (isLhDevice(area) ||
      isLhCoreRegion(area) ||
      isLhMmapRegion(area) ||
      isLhMpiInitRegion(area)) {
    return true;
  }
  /******************************************************************
   * FIXME:                                                         *
   *   So far, the upper-half example programs don't create their   *
   *   own /anon_hugepage.  That could change in the future.        *
   *   For now, isLhRegion(&area) is always true for /anon_hugepage *
   *   /anon_hugepage created by ioctl in MPI_Init, and so we can't *
   *   easily interpose on it.                                      *
   ******************************************************************/
  if (strstr(area->name, "/anon_hugepage")) {
    return true;
  }
  return false;
}

EXTERNC int
dmtcp_skip_memory_region_ckpting(const ProcMapsArea *area)
{
  if (isLhDevice(area)) {
    JTRACE("Ignoring region")(area->name)((void*)area->addr);
    return 1;
  }

  if (isLhCoreRegion(area)) {
    JTRACE ("Ignoring LH core region") ((void*)area->addr);
    return 1;
  }

  if (isLhMmapRegion(area)) {
    JTRACE("Ignoring LH mmap region")
    (area->name)((void *)area->addr)(area->size);
    return 1;
  }

  /*  FIXME:  This routine was finding a false positive.  It was skipping
   *          the saving of one of the memory area beyond '[stack]'.
   *          This was exhibited after Perlmutter was upgraded on Aug. 16, 2023.
   *  if (isLhMpiInitRegion(area)) {
   *    JTRACE("Ignoring MpiInit region")(area->name)((void*)area->addr);
   *    return 1;
   *  }
   */

  if (strstr(area->name, "/anon_hugepage")) {
    JTRACE("Ignoring /anon_hugepage region")(area->name)((void*)area->addr);
    return 1;
  }

  return 0;
}

EXTERNC int
dmtcp_skip_truncate_file_at_restart(const char* path)
{
  constexpr const char* P2P_LOG_MSG = "p2p_log_%d.txt";
  constexpr const char* P2P_LOG_REQUEST = "p2p_log_request_%d.txt";
  char p2p_log_name[100];
  char p2p_log_request_name[100];
  int rank;

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  snprintf(p2p_log_name, sizeof(p2p_log_name) - 1, P2P_LOG_MSG, rank);
  snprintf(p2p_log_request_name, sizeof(p2p_log_request_name)-1,
           P2P_LOG_REQUEST, rank);

  if (strstr(path, p2p_log_name) ||
      strstr(path, p2p_log_request_name)) {
    // Do not truncate this file.
    return 1;
  }

  // Defer to the next plugin.
  return NEXT_FNC(dmtcp_skip_truncate_file_at_restart)(path);
}

// TODO(kapil): Replace with Jassert::PrintBackrace.
string GetBacktrace()
{
  ostringstream ss;
  void *buffer[BT_SIZE];
  int nptrs = backtrace(buffer, BT_SIZE);

  ss << "Backtrace:\n";
  char buf[1024];

  for (int i = 1; i < nptrs; i++) {
    Dl_info info;
    ss << "   " << i << ' ' ;
    if (dladdr1(buffer[i], &info, NULL, 0)) {
      int status;
      size_t buflen = sizeof(buf);
      buf[0] = '\0';
      if (info.dli_sname) {
        //  WAS: 'char *demangled =' (but compiler issues 'not used' warning.
        abi::__cxa_demangle(info.dli_sname, buf, &buflen, &status);
        if (status != 0) {
          strncpy(buf, info.dli_sname, sizeof(buf) - 1);
        }
      }

      ss << (buf[0] ? buf : "") << " in " << info.dli_fname << " ";
    }
    ss << jalib::XToHexString(buffer[i]) << "\n";
  }

  return ss.str();
}

constexpr int MaxSignals = 64;
static struct sigaction userSignalHandlers[MaxSignals] = {0};

void
mana_signal_sa_handler_wrapper(int signum)
{
  unsigned long fsbase = getFS();

  DmtcpMutexLock(&g_upper_half_fsbase_lock);
  unsigned long uh_fsbase = g_upper_half_fsbase->at(dmtcp_get_real_tid());
  DmtcpMutexUnlock(&g_upper_half_fsbase_lock);

  if (fsbase != uh_fsbase) {
    // We are in lower-half; switch to upper half.
    setFS(uh_fsbase);
  }

  userSignalHandlers[signum].sa_handler(signum);

  if (fsbase != uh_fsbase) {
    // go back to lower-half.
    setFS(fsbase);
  }
}

void
mana_signal_sa_sigaction_wrapper(int signum, siginfo_t *siginfo, void *context)
{
  unsigned long fsbase = getFS();
  DmtcpMutexLock(&g_upper_half_fsbase_lock);
  unsigned long uh_fsbase = g_upper_half_fsbase->at(dmtcp_get_real_tid());
  DmtcpMutexUnlock(&g_upper_half_fsbase_lock);

  if (fsbase != uh_fsbase) {
    // We are in lower-half; switch to upper half.
    setFS(uh_fsbase);
  }

  if (signum == SIGTERM) {
    JNOTE("SIGTERM received. Exiting.")(GetBacktrace());
    exit(1);
  }

  userSignalHandlers[signum].sa_sigaction(signum, siginfo, context);

  if (fsbase != uh_fsbase) {
    // go back to lower-half.
    setFS(fsbase);
  }
}

EXTERNC int
sigaction(int signum, const struct sigaction *act, struct sigaction *oldAct)
{
  int ret;
  if (signum == dmtcp_get_ckpt_signal() ||
      signum >= MaxSignals) {
    return NEXT_FNC(sigaction)(signum, act, oldAct);
  }

  if (act != NULL) {
    struct sigaction newAct = *act;

    if (newAct.sa_flags & SA_SIGINFO) {
      newAct.sa_sigaction = mana_signal_sa_sigaction_wrapper;
      ret = NEXT_FNC(sigaction)(signum, &newAct, oldAct);
    } else {
      if (newAct.sa_handler == SIG_IGN || newAct.sa_handler == SIG_DFL) {
        return NEXT_FNC(sigaction)(signum, act, oldAct);
      }

      newAct.sa_handler = mana_signal_sa_handler_wrapper;
      ret = NEXT_FNC(sigaction)(signum, &newAct, oldAct);
    }

    if (ret == 0) {
      userSignalHandlers[signum] = *act;
    }
  } else {
    ret = NEXT_FNC(sigaction)(signum, act, oldAct);
  }

  if (oldAct != NULL && ret == 0) {
    *oldAct = userSignalHandlers[signum];
  }

  return ret;
}

EXTERNC sighandler_t
signal(int signum, sighandler_t handler)
{
  sighandler_t ret;
  if (signum == dmtcp_get_ckpt_signal() ||
      signum >= MaxSignals) {
    return NEXT_FNC(signal)(signum, handler);
  }

  if (handler == SIG_IGN || handler == SIG_DFL) {
    ret = NEXT_FNC(signal)(signum, handler);
  } else {
    ret = NEXT_FNC(signal)(signum, mana_signal_sa_handler_wrapper);
  }

  if (ret != SIG_ERR) {
    userSignalHandlers[signum].sa_handler = handler;
  }

  return ret;
}

void
initialize_signal_handlers()
{
  for (int i = 0; i < MaxSignals; i++) {
    NEXT_FNC(sigaction)(i, NULL, &userSignalHandlers[i]);
  }
}

// Handler for SIGSEGV: forces the code into an infinite loop for attaching
// GDB and debugging
void
segvfault_handler(int signum, siginfo_t *siginfo, void *context)
{
  int dummy = 0;
  JNOTE("Caught a segmentation fault. Attach gdb to inspect...");
  while (!dummy);
}

// Installs a handler for SIGSEGV; useful for debugging crashes
void
initialize_segv_handler()
{
  const char* segvLoop = getenv(MANA_SEGV_DEBUG_LOOP);
  if (!segvLoop || segvLoop[0] != '1') {
    return;
  }

  static struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_flags = SA_SIGINFO;
  action.sa_sigaction = segvfault_handler;
  sigemptyset(&action.sa_mask);

  JASSERT(sigaction(SIGSEGV, &action, NULL) != -1)
    (JASSERT_ERRNO).Text("Could not set up the segfault handler");
}

// Sets the lower half's __environ variable to point to upper half's __environ
static void
updateLhEnviron()
{
  updateEnviron_t fnc = (updateEnviron_t)lh_info.updateEnvironFptr;
  fnc(__environ);
}

static void
computeUnionOfCkptImageAddresses()
{
  ProcSelfMaps procSelfMaps; // This will be deleted when out of scope.
  Area area;

  // This code assumes that /proc/sys/kernel/randomize_va_space is 0
  //   or that personality(ADDR_NO_RANDOMIZE) was set.
  char buf;
  JWARNING(Util::readAll("/proc/sys/kernel/randomize_va_space", &buf, 1) != 1 ||
           buf == '0' ||
           (personality(0xffffffff) & ADDR_NO_RANDOMIZE) == 0)
    .Text("FIXME:  Linux process randomization is turned on.\n");

  // Initialize the MPI information to NULL (unknown).
  void *libsStart = NULL;
  void *minLibsStart = NULL;
  void *libsEnd = NULL;
  void *maxLibsEnd = NULL;
  void *highMemStart = 0x0;
  void *minHighMemStart = NULL;
  void *lhEnd = 0x0; // lower bound for actual lhEnd
  void *minAddrBeyondHeap = NULL;
  void *maxAddrBeyondHeap = NULL;

  // This is an upper bound on libsStart.  But if there is a hole in the
  //   upper-half memory layout, then this could be a _very_ high upper bound.
  libsStart = (char *)mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
                           -1, 0);
  JASSERT((void *)libsStart != MAP_FAILED);
  munmap(libsStart, 4096);
  //fprintf(stderr, "--- Initial libsStart is %p.\n", libsStart);
  // Preprocess memory regions as needed.
  bool foundLhMmapRegion = false;
  void *prev_addr_end = NULL;
  while (procSelfMaps.getNextArea(&area)) {
    if (isLhMmapRegion(&area)) {
      foundLhMmapRegion = true;
    }
    // ASSUMPTION:  LAYOUT: upperHalfExecutable, lh_proxy, lhMmapRegion,
    //                 /anon_hugepage (if ioctl from MPI_Init), upperHalfLibs
    // NOTE: In VASP, we've seen:
    //          [vvar], [vdso], lib/ld.so, [stack], argv/env/auxv, [vsyscall]
    //  Normally, we see:
    //          lib/ld.so, [vvar], [vdso], [stack], argv/env/auxv, [vsyscall]
    /**
     * FIXME: 0x11200000 is the address for mtcp_restart, find the address more
     * properly
    */
    if (foundLhMmapRegion && area.addr < libsStart && !isLhRegion(&area) &&
        !(area.addr >= (char *) 0x11200000 && area.addr <= (char*) 0x11213000)) {
      libsStart = area.addr;
    } else if (isLhRegion(&area) && area.addr < libsStart) {
      // libsStart is an upper bound, but at worst, it's in middle of uh libs
      lhEnd = area.endAddr;  // lower bound on lhEnd
    }
    // If this area looks like a lib*.so or lib*.so.*, then it's a library.
    // Set libsEnd to the endAddress of the last library.
    if (!isLhRegion(&area) &&
         ( Util::strEndsWith(area.name, ".so") ||
           ( strstr(area.name, ".so.") != NULL &&
             // Why is this needed?  What is an example of "/var/lib/*.so.*"?
             ! Util::strStartsWith(area.name, "/var/lib")))) {
      // FIXME:  The upper half may include addresses beyond last lib*.so file
      //         We're looking for the gap between [libsStart, libsEnd]
      //         and [minHighMem, end_of_memory_in_restart_plugin]
      // NOTE: /anon_hugepage is created by MPI_Init and placed as high as
      //       possible on Perlmutter.  At launch, this is _below_ all the
      //       libraries, since MPI_Init is called after loading all libraries.
      //       But on restart, we create lower-half first.  We will reserve all
      //       of upper half, and so force /anon_hugepage again to lower addr.
      libsEnd = area.endAddr;
    }
    // Set mtcpHdr->highMemStart if possible.
    // restart_plugin will remap [vvar] and [vdso] out of way when
    // we reserve memory before calling MPI_Init.  And [vsyscall] always
    // comes after [stack].  So, beginning of [stack] should be highMemStart.
    /**
     * FIXME: We want to set highMemStart to beginning of '[stack]' region.
     *         On a second restart, '[stack]' is no longer labeled.
     *         So, we guess that it's the next memory segment after libsEnd.
     *         We should at least verify that '$sp' is in this memory region.
     * FIXME: A more robust fix is to remember the stack segment at the
     *         time of first checkpoint using the "[stack]" label.  This can
     *         be done inside this file.  Then we can save that as a
     *         static global variable, update it in case the stack grew,
     *         and then re-use that information here, during a second restart.
     *  if (strcmp(area.name, "[stack]") == 0)
     */
    if (prev_addr_end != NULL && prev_addr_end == libsEnd)
    {
      highMemStart = area.addr; // This should be the start of the stack.
    }
    if (strcmp(area.name, "[stack]") == 0)
    {
      highMemStart = area.addr; // This should be the start of the stack.
    }
    // FIXME:  We are no longer using min/maxAddrBeyondHeap.
    //         Given that heapAddr is poorly defined between launch and restart,
    //         we should delete this code and all min/maxAddrBeyondHeap,
    //         when we're convinced it's not needed.
    if (area.addr > heapAddr &&
        !isLhRegion(&area)) {
      if (minAddrBeyondHeap == nullptr) {
        minAddrBeyondHeap = area.addr;
      }
      if (strcmp(area.name, "[vsyscall]") != 0) {
        maxAddrBeyondHeap = area.endAddr;
      }
    }
    prev_addr_end = area.endAddr;
  }

  JASSERT(lhEnd <= libsStart)(lhEnd)(libsStart);
  // Adjust libsStart to make 4GB of space in which to grow.
  void *origLibsStart = libsStart;
  char *libsStartTmp = (char *)libsStart; // Stupid C++; Error if 'void *'
  if ((uint64_t)((char *)lhEnd - libsStartTmp) > 5*ONEGB) {
    libsStartTmp = libsStartTmp - 4*ONEGB;
    // Round down to multiple of ONEGB:  Easy to recognize address.
    libsStartTmp = (char *)((uint64_t)libsStartTmp & (uint64_t)(~(ONEGB-1)));
  } else { // Else choose libsStart halfway between lhEnd and current libsStart
    libsStartTmp = libsStartTmp - (libsStartTmp - (char *)lhEnd)/2;
  }
  libsStart = libsStartTmp;

  JTRACE("Layout of memory regions during ckpt")
        ((void *)lhEnd)((void *)libsStart)(origLibsStart);

  string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));
  string origLibsStartStr = jalib::XToString(origLibsStart);
  string heapAddrStr = jalib::XToString(heapAddr);
  string libsStartStr = jalib::XToString(libsStart);
  string libsEndStr = jalib::XToString(libsEnd);
  string minAddrBeyondHeapStr = jalib::XToString(minAddrBeyondHeap);
  string maxAddrBeyondHeapStr = jalib::XToString(maxAddrBeyondHeap);
  string highMemStartStr = jalib::XToString(highMemStart);
  string lhMemStart = jalib::XToString(lh_info.memRange.start);
  string lhMemEnd = jalib::XToString(lh_info.memRange.end);

  kvdb::set(workerPath, "MANA_heapAddr", heapAddrStr);
  kvdb::set(workerPath, "MANA_libsStart_Orig", origLibsStartStr);
  kvdb::set(workerPath, "MANA_libsStart", libsStartStr);
  kvdb::set(workerPath, "MANA_libsEnd", libsEndStr);
  kvdb::set(workerPath, "MANA_highMemStart", highMemStartStr);
  kvdb::set(workerPath, "MANA_minAddrBeyondHeap", minAddrBeyondHeapStr);
  kvdb::set(workerPath, "MANA_maxAddrBeyondHeap", maxAddrBeyondHeapStr);
  kvdb::set(workerPath, "MANA_LowerHalf_Start", lhMemStart);
  kvdb::set(workerPath, "MANA_LowerHalf_End", lhMemEnd);

  constexpr const char *kvdb = "/plugin/MANA/CKPT_UNION";
  JASSERT(kvdb::request64(KVDBRequest::MIN, kvdb, "libsStart",
                          (int64_t)libsStart) == KVDBResponse::SUCCESS);
  JASSERT(kvdb::request64(KVDBRequest::MAX, kvdb, "libsEnd",
                          (int64_t)libsEnd) == KVDBResponse::SUCCESS);
  JASSERT(kvdb::request64(KVDBRequest::MIN, kvdb, "highMemStart",
                          (int64_t)highMemStart) == KVDBResponse::SUCCESS);

  dmtcp_global_barrier("MANA_CKPT_UNION");

  JASSERT(kvdb::get64(kvdb, "libsStart", (int64_t *)&minLibsStart) ==
          KVDBResponse::SUCCESS);
  JASSERT(kvdb::get64(kvdb, "libsEnd", (int64_t *)&maxLibsEnd) ==
          KVDBResponse::SUCCESS);
  JASSERT(kvdb::get64(kvdb, "highMemStart", (int64_t *)&minHighMemStart) ==
          KVDBResponse::SUCCESS);

  ostringstream o;

#define HEXSTR(o, x) o << #x << std::hex << x;
  HEXSTR(o, libsStart);
  HEXSTR(o, libsEnd);
  HEXSTR(o, highMemStart);
  HEXSTR(o, minLibsStart);
  HEXSTR(o, maxLibsEnd);
  HEXSTR(o, minHighMemStart);

  JTRACE("Union of memory regions") (o.str());

  string minLibsStartStr = jalib::XToString(minLibsStart);
  string maxLibsEndStr = jalib::XToString(maxLibsEnd);
  string minHighMemStartStr = jalib::XToString(minHighMemStart);

  // Now publish these values to DMTCP ckpt-header.
  dmtcp_add_to_ckpt_header("MANA_MinLibsStart", minLibsStartStr.c_str());
  dmtcp_add_to_ckpt_header("MANA_MaxLibsEnd", maxLibsEndStr.c_str());
  dmtcp_add_to_ckpt_header("MANA_MinHighMemStart", minHighMemStartStr.c_str());
  // Not used
  // dmtcp_add_to_ckpt_header("MANA_MaxHighMemEnd", maxHighMemEndStr.c_str());

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  string rankStr = jalib::XToString(rank);
  kvdb::set(workerPath, "MPI_Rank", rankStr);

  if (getenv("MANA_DEBUG")) {
    if (rank == 0) {
      char time_string[30];
      time_t t = time(NULL);
      strftime(time_string, sizeof(time_string), "%H:%M:%S", localtime(&t));
      fprintf(stderr,
              "\n%s: *** MANA: Checkpointing; MANA info saved for restart:\n"
              "%*c    (Unset env var MANA_DEBUG to silence this.)\n",
              time_string, (int)strlen(time_string), ' ');
      fprintf(stderr, "  %s: %s\n",
                      "MANA_MinLibsStart", minLibsStartStr.c_str());
      fprintf(stderr, "  %s: %s\n",
                      "MANA_MaxLibsEnd", maxLibsEndStr.c_str());
      fprintf(stderr, "%s: %s\n",
                      "  MANA_MinHighMemStart", minHighMemStartStr.c_str());
      fprintf(stderr, "  Starting to checkpoint ...\n");
    }
  }
}

const char *
get_mana_header_file_name()
{
  struct stat st;
  const char *ckptDir;
  dmtcp::ostringstream o;

  ckptDir = dmtcp_get_ckpt_dir();
  if (stat(ckptDir, &st) == -1) {
    mkdir(ckptDir, 0700); // Create directory if not already exist
  }
  o << ckptDir << "/header.mana";
  return strdup(o.str().c_str());
}

void
save_mana_header(const char *filename)
{
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd == -1) {
    return;
  }

  write(fd, &g_mana_header.init_flag, sizeof(int));
  close(fd);
}

const char *
get_mpi_file_filename()
{
  struct stat st;
  const char *ckptDir;
  dmtcp::ostringstream o;

  ckptDir = dmtcp_get_ckpt_dir();
  if (stat(ckptDir, &st) == -1) {
    mkdir(ckptDir, 0700); // Create directory if not already exist
  }
  o << ckptDir << "/mpi_files.mana";
  return strdup(o.str().c_str());
}

static void
save_mpi_files(const char *filename)
{
  // No need to save open files if there are no files to save
  if (g_params_map.size() == 0) return;

  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd == -1) {
    return;
  }

  for (auto itr = g_params_map.begin(); itr != g_params_map.end(); itr++) {
    // Only the most recent value of these properties needs to be saved
    MPI_File_get_position(itr->first, &itr->second._offset);
    MPI_File_get_atomicity(itr->first, &itr->second._atomicity);
    MPI_File_get_size(itr->first, &itr->second._size);

    // Note that a garbage view may be set by this call if set_view was never
    // called, but that view won't be restored
    MPI_File_get_view(itr->first, &itr->second._viewDisp,
                      &itr->second._viewEType, &itr->second._viewFType,
                      itr->second._datarep);

    // Save both virtual ID and corresponding parameters
    write(fd, &itr->first, sizeof(MPI_File));
    write(fd, &itr->second, sizeof(struct OpenFileParameters));
  }

  close(fd);
}

static void
restore_mpi_files(const char *filename)
{
  int fd = open(filename, O_RDONLY, 0755);

  // It's fine if the file doesn't exist. This just means there's no files
  // to be restored.
  if (fd == -1) {
    return;
  }

  // Restore the g_params_map with each virtual file and its corresponding
  // parameters from the mapping file
  MPI_File file_buf;
  struct OpenFileParameters params_buf;
  g_params_map.clear();
  size_t bytes_read = 0;
  do {
    bytes_read = read(fd, &file_buf, sizeof(MPI_File));
    bytes_read = read(fd, &params_buf, sizeof(OpenFileParameters));
    g_params_map[file_buf] = params_buf;
  } while(bytes_read != 0);
  close(fd);

  // Restore MPI-created files
  for (auto itr = g_params_map.begin(); itr != g_params_map.end(); itr++) {
    // Re-create file with initial parameters
    int retval;
    MPI_File fh;
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(itr->second._comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(File_open)(realComm, itr->second._filepath,
                                  itr->second._mode, itr->second._info, &fh);
    RETURN_TO_UPPER_HALF();
    JASSERT(retval == 0).Text("Restoration of MPI_File_open failed");

    // Update virtual mapping with newly created file
    UPDATE_FILE_MAP(itr->first, fh);

    // Restore file characteristics that are universal to the entire file
    // These are characteristics that do not have to be replayed in order,
    // because only the most recent one will affect the behavior of the file

    // Note that the order of restoration is extremely important (the file size)
    // MUST be set before the view or offset
    MPI_File_set_atomicity(fh, itr->second._atomicity);
    MPI_File_set_size(fh, itr->second._size);

    // Only restore view if required
    if (itr->second._viewSet) {
      MPI_File_set_view(fh, itr->second._viewDisp, itr->second._viewEType,
                        itr->second._viewFType, itr->second._datarep,
                        itr->second._viewInfo);
    }
    MPI_File_seek(fh, itr->second._offset, MPI_SEEK_SET);

  }

}

#ifdef SINGLE_CART_REORDER
const char *
get_cartesian_properties_file_name()
{
  struct stat st;
  const char *ckptDir;
  dmtcp::ostringstream o;

  ckptDir = dmtcp_get_ckpt_dir();
  if (stat(ckptDir, &st) == -1) {
    mkdir(ckptDir, 0700); // Create directory if not already exist
  }
  o << ckptDir << "/cartesian.info";
  return strdup(o.str().c_str());
}

void
save_cartesian_properties(const char *filename)
{
  if (g_cartesian_properties.comm_old_size == -1 ||
      g_cartesian_properties.comm_cart_size == -1 ||
      g_cartesian_properties.comm_old_rank == -1 ||
      g_cartesian_properties.comm_cart_rank == -1) {
    return;
  }
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd == -1) {
    return;
  }
  write(fd, &g_cartesian_properties.comm_old_size, sizeof(int));
  write(fd, &g_cartesian_properties.comm_cart_size, sizeof(int));
  write(fd, &g_cartesian_properties.comm_old_rank, sizeof(int));
  write(fd, &g_cartesian_properties.comm_cart_rank, sizeof(int));
  write(fd, &g_cartesian_properties.reorder, sizeof(int));
  write(fd, &g_cartesian_properties.ndims, sizeof(int));
  int array_size = sizeof(int) * g_cartesian_properties.ndims;
  write(fd, g_cartesian_properties.coordinates, array_size);
  write(fd, g_cartesian_properties.dimensions, array_size);
  write(fd, g_cartesian_properties.periods, array_size);
  close(fd);
}
#endif

void printElapsedTime(time_t origin_time, const char *msg) {
  char time_string[30];
  time_t cur_time = time(NULL);
  time_t delta_time = cur_time - origin_time;
  strftime(time_string, sizeof(time_string),
           "%H:%M:%S", localtime(&cur_time));
  if (msg != NULL) {
    fprintf(stderr, "%s: *** MANA: %s\n", time_string, msg);
    if (origin_time == 0) { return; } // We only print msg, not elapsed time
    fprintf(stderr, "%*c      ", (int)strlen(time_string), ' ');
  } else {
    fprintf(stderr, "%s:     ", time_string);
  }
  fprintf(stderr, "Elapsed time since INIT/RESTART: %ld seconds\n", delta_time);
  return;
}

// FIXME:  Use 'getenv("MANA_TIMING")' instead in 'if' stmt.
//         In bin/mana_launch, add '--timing' flag that sets this env. var.
void printEventToStderr(const char *msg) {
  if (!getenv("MANA_TIMING")) { return; }

  static time_t init_time = 0;
  if (init_time == 0 && strstr(msg, "INIT")) {
    init_time = time(NULL);
    printElapsedTime(0, msg);
    return;  // Only one process should print, but we don't yet have a rank.
  }
  int rank = g_world_rank; // MPI_Comm_rank would fail at DMTCP_EVENT_EXIT.
  if (rank == 0) {
    if (strstr(msg, "RESTART")) {
      init_time = time(NULL);
      printElapsedTime(0, msg);
    } else {
      printElapsedTime(init_time, msg);
    }
  }
}

static void
mpi_plugin_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT: {
      printEventToStderr("EVENT_INIT"); // We don't have a rank. So no printing.
      JTRACE("*** DMTCP_EVENT_INIT");
      JASSERT(dmtcp_get_real_tid != NULL);
      initialize_signal_handlers();
      initialize_segv_handler();
      JASSERT(!splitProcess()).Text("Failed to create, initialize lower half");
      seq_num_init();
      mana_state = RUNNING;

      DmtcpMutexInit(&g_upper_half_fsbase_lock, DMTCP_MUTEX_LLL);
      if (g_upper_half_fsbase != NULL) {
        delete g_upper_half_fsbase;
      }

      g_upper_half_fsbase = new std::unordered_map<pid_t, unsigned long>();
      g_upper_half_fsbase->insert(std::make_pair(dmtcp_get_real_tid(), getFS()));

      if (g_file_flags_map != NULL) {
        delete g_file_flags_map;
      }
      g_file_flags_map = new map<string, int>();

      // TODO(kapil): Remove from final commit.
      setenv(MANA_FILE_REGEX_ENV, ".*", 1);

      if (file_regex == NULL && getenv(MANA_FILE_REGEX_ENV) != NULL) {
        file_regex = new std::regex(getenv(MANA_FILE_REGEX_ENV));
      }

      if (CheckAndEnableFsGsBase()) {
        JTRACE("FSGSBASE enabled");
      }

      heapAddr = sbrk(0);
      JASSERT(heapAddr != nullptr);
      // FIXME:  If we use PROT_NONE (preferred), then an older DMTCP
      //         will not restore this memory region.
      // By creating a memory page just beyond the end of the heap,
      // this will prevent glibc from extending the main malloc arena.
      // So, glibc will create a second arena.
      // NOTE:  This is needed for mtcp_restart, lh_proxy, and the upper half.
      // Rationale:  On restart, the end-of-heap is not here, but at
      //             the end-of-heap for mtcp_restart.  Luckily, glibc
      //             caches 'sbrk(0)', which is here.  So, glibc should
      //             detect that there is an mmap'ed region just beyond it,
      //             thus causing glibc to allocate a second arena elsewhere.
      mmap(heapAddr, 4096, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

      break;
    }
    case DMTCP_EVENT_EXIT: {
      printEventToStderr("EVENT_EXIT");
      JTRACE("*** DMTCP_EVENT_EXIT");
      seq_num_destroy();
      break;
    }

    case DMTCP_EVENT_OPEN_FD: {
      processFileOpen(data->openFd.path, data->openFd.flags);
      break;
    }

    case DMTCP_EVENT_PTHREAD_START: {
      // Do we need a mutex here?
      DmtcpMutexLock(&g_upper_half_fsbase_lock);
      g_upper_half_fsbase->insert(std::make_pair(dmtcp_get_real_tid(), getFS()));
      DmtcpMutexUnlock(&g_upper_half_fsbase_lock);
      break;
    }

    case DMTCP_EVENT_PTHREAD_RETURN:
    case DMTCP_EVENT_PTHREAD_EXIT: {
      // Do we need a mutex here?
      DmtcpMutexLock(&g_upper_half_fsbase_lock);
      g_upper_half_fsbase->erase(dmtcp_get_real_tid());
      DmtcpMutexUnlock(&g_upper_half_fsbase_lock);
      break;
    }

    case DMTCP_EVENT_PRESUSPEND: {
      printEventToStderr("EVENT_PRESUSPEND (finish collective op's)");
      mana_state = CKPT_COLLECTIVE;
      // preSuspendBarrier() will send coord response and get worker state.
      // FIXME:  See commant at: dmtcpplugin.cpp:'case DMTCP_EVENT_PRESUSPEND'
      drain_mpi_collective();
      openCkptFileFds();
      printEventToStderr("EVENT_PRESUSPEND (done)");
      break;
    }

    case DMTCP_EVENT_PRECHECKPOINT: {
      printEventToStderr("EVENT_PRECHECKPOINT (drain send/recv)");
      recordMpiInitMaps();
      recordOpenFds();
      dmtcp_local_barrier("MPI:GetLocalRankInfo");
      getLocalRankInfo(); // p2p_log_replay.cpp
      dmtcp_global_barrier("MPI:update-ckpt-dir-by-rank");
      updateCkptDirByRank(); // mpi_plugin.cpp
      dmtcp_global_barrier("MPI:Register-local-sends-and-receives");
      mana_state = CKPT_P2P;
      registerLocalSendsAndRecvs(); // p2p_drain_send_recv.cpp
      dmtcp_global_barrier("MPI:Drain-Send-Recv");
      drainSendRecv(); // p2p_drain_send_recv.cpp
      computeUnionOfCkptImageAddresses();
      dmtcp_global_barrier("MPI:save-mana-header-and-mpi-files");
      const char *file = get_mana_header_file_name();
      save_mana_header(file);
      const char *file2 = get_mpi_file_filename();
      save_mpi_files(file2);
#ifdef SINGLE_CART_REORDER
      dmtcp_global_barrier("MPI:save-cartesian-properties");
      const char *file = get_cartesian_properties_file_name();
      save_cartesian_properties(file);
#endif
      printEventToStderr("EVENT_PRECHECKPOINT (done)");
      // Save a copy of the break address before checkpoint
      old_brk = sbrk(0);
      break;
    }

    case DMTCP_EVENT_RESUME: {
      int rank;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      if (getenv("MANA_DEBUG")) {
        if (rank == 0) {
          fprintf(stderr, "  ... resuming from checkpoint.\n");
        }
      }
      printEventToStderr("EVENT_RESUME");
      processingOpenCkpFileFds = false;
      dmtcp_local_barrier("MPI:Reset-Drain-Send-Recv-Counters");
      resetDrainCounters(); // p2p_drain_send_recv.cpp
      seq_num_reset(RESUME);
      dmtcp_local_barrier("MPI:seq_num_reset");
      mana_state = RUNNING;
      printEventToStderr("EVENT_RESUME (done)");
      break;
    }

    case DMTCP_EVENT_RESTART: {
      // Restore saved break adress to reset heap
      // FIXME: On Perlmutter, the begining address of mtcp_restart's heap
      // is higher than the saved break address. Therefore, calling brk
      // in mtcp_restart cannot properly reset the break address (and heap)
      // of the restored program. Calling brk() again here can avoid a
      // segfault on restart. But the restored program will still use
      // mtcp_restart's break address, instead of the saved break address
      // as the new heap. 
      brk(old_brk);
      printEventToStderr("EVENT_RESTART");
      processingOpenCkpFileFds = false;
      logCkptFileFds();

      mpiInitLhAreas->clear();

      g_upper_half_fsbase->clear();
      g_upper_half_fsbase->insert(std::make_pair(dmtcp_get_real_tid(), getFS()));

      dmtcp_local_barrier("MPI:updateEnviron");
      updateLhEnviron(); // mpi-plugin.cpp
      updateVdsoLinkmapEntry(lh_info.vdsoLdAddrInLinkMap);
      dmtcp_local_barrier("MPI:Reset-Drain-Send-Recv-Counters");
      resetDrainCounters(); // p2p_drain_send_recv.cpp
      char str[1];
      MPI_MANA_Internal(str); // This does nothing.  Modify in lower-half
                             // for easy debugging of lower half during restart.
                             // See definition in mpi-wrappers/mpi_wrappers.cpp
      mana_state = RESTART_REPLAY;
#ifdef SINGLE_CART_REORDER
      dmtcp_global_barrier("MPI:setCartesianCommunicator");
      // record-replay.cpp
      setCartesianCommunicator(lh_info.getCartesianCommunicatorFptr);
#endif
      dmtcp_global_barrier("MPI:restoreMpiLogState");
      restoreMpiLogState(); // record-replay.cpp
      dmtcp_global_barrier("MPI:record-replay.cpp-void");
      replayMpiP2pOnRestart(); // p2p_log_replay.cpp
      dmtcp_local_barrier("MPI:p2p_log_replay.cpp-void");
      seq_num_reset(RESTART);
      dmtcp_local_barrier("MPI:seq_num_reset");
      const char *file = get_mpi_file_filename();
      restore_mpi_files(file);
      dmtcp_local_barrier("MPI:Restore-MPI-Files");
      mana_state = RUNNING;
      printEventToStderr("EVENT_RESTART (done)");
      break;
    }

    case DMTCP_EVENT_RUNNING: {
      closeCkptFileFds();
      break;
    }

    case DMTCP_EVENT_THREAD_RESUME: {
      DmtcpMutexLock(&g_upper_half_fsbase_lock);
      g_upper_half_fsbase->insert(std::make_pair(dmtcp_get_real_tid(), getFS()));
      DmtcpMutexUnlock(&g_upper_half_fsbase_lock);
      break;
    }

    default: {
      break;
    }
  }
}

DmtcpPluginDescriptor_t mpi_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "mpi_plugin",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "MPI Proxy Plugin",
  mpi_plugin_event_hook
};

DMTCP_DECL_PLUGIN(mpi_plugin);
