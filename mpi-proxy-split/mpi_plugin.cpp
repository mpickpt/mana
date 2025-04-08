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
#include "lower-half-api.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "record-replay.h"
#include "seq_num.h"
#include "mpi_nextfunc.h"
#include "virtual_id.h"
#include "uh_wrappers.h"
#include "logging.h"

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

void * lh_ckpt_mem_addr = NULL;
size_t lh_ckpt_mem_size = 0;
int pagesize = sysconf(_SC_PAGESIZE);
get_mmapped_list_fptr_t get_mmapped_list_fnc = NULL;
std::vector<MmapInfo_t> uh_mmaps;

extern ManaHeader g_mana_header;
extern std::unordered_map<MPI_File, OpenFileParameters> g_params_map;

bool g_libmana_is_initialized = false;

constexpr const char *MANA_FILE_REGEX_ENV = "MANA_FILE_REGEX";
constexpr const char *MANA_SEGV_DEBUG_LOOP = "MANA_SEGV_DEBUG_LOOP";
static std::regex *file_regex = NULL;
static map<string, int> *g_file_flags_map = NULL;
static vector<int> ckpt_fds;
static bool processingOpenCkpFileFds = false;

std::unordered_map<pid_t, unsigned long> *g_upper_half_fsbase = NULL;
DmtcpMutex g_upper_half_fsbase_lock;
int CheckAndEnableFsGsBase();

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

void *old_brk;
void *old_end_of_brk;
char *uh_stack_start;
char *uh_stack_end;

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

EXTERNC int
dmtcp_skip_memory_region_ckpting(ProcMapsArea *area)
{
  if (isLhDevice(area)) {
    JTRACE("Ignoring region")(area->name)((void*)area->addr);
    return 1;
  }

  if (strstr(area->name, "heap")) {
    JTRACE("Ignoring heap region")(area->name)((void*)area->addr);
    return 1;
  }

  if (strstr(area->name, "/anon_hugepage")) {
    JTRACE("Ignoring /anon_hugepage region")(area->name)((void*)area->addr);
    return 1;
  }

  // If it's the upper-half stack, don't skip
  if (area->addr >= lh_info->uh_stack_start && area->endAddr <= lh_info->uh_stack_end) {
    return 0;
  }

  get_mmapped_list_fnc = (get_mmapped_list_fptr_t) lh_info->mmap_list_fptr;

  int numUhRegions;
  if (uh_mmaps.size() == 0) {
    uh_mmaps = get_mmapped_list_fnc(&numUhRegions);
  }

  for (MmapInfo_t &region : uh_mmaps) {
    if (regionContains(region.addr, region.addr + region.len,
                       area->addr, area->endAddr)) {
      return 0;
    }
    if (regionContains(area->addr, area->endAddr,
                       region.addr, region.addr + region.len)) {
      area->addr = (char*) region.addr;
      area->endAddr = (char*) (region.addr + region.len);
      area->size = area->endAddr - area->addr;
      return 0;
    }
    if (area->addr < region.addr && region.addr < area->endAddr &&
        area->endAddr < region.addr + region.len) {
      area->addr = (char*) region.addr;
      area->size = area->endAddr - area->addr;
      return 0;
    }
    if (region.addr < area->addr && area->addr < region.addr + region.len &&
        region.addr + region.len < area->endAddr) {
      area->endAddr = (char*) (region.addr + region.len);
      area->size = area->endAddr - area->addr;
      return 0;
    }
  }
  return 1;
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

  unsigned long uh_fsbase;
  DmtcpMutexLock(&g_upper_half_fsbase_lock);
  pid_t real_tid = dmtcp_get_real_tid();
  std::unordered_map<pid_t, unsigned long>::iterator it= g_upper_half_fsbase->find(real_tid);
  if (it == g_upper_half_fsbase->end()) {
    uh_fsbase = getFS();
    g_upper_half_fsbase->insert(std::make_pair(real_tid, uh_fsbase));
  } else {
    uh_fsbase = it->second;
  }
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
  unsigned long uh_fsbase;
  DmtcpMutexLock(&g_upper_half_fsbase_lock);
  pid_t real_tid = dmtcp_get_real_tid();
  std::unordered_map<pid_t, unsigned long>::iterator it= g_upper_half_fsbase->find(real_tid);
  if (it == g_upper_half_fsbase->end()) {
    uh_fsbase = getFS();
    g_upper_half_fsbase->insert(std::make_pair(real_tid, uh_fsbase));
  } else {
    uh_fsbase = it->second;
  }
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
    MPI_Comm real_comm = get_real_id((mana_mpi_handle){.comm = itr->second._comm}).comm;
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    retval = NEXT_FUNC(File_open)(real_comm, itr->second._filepath,
                                  itr->second._mode, itr->second._info, &fh);
    RETURN_TO_UPPER_HALF();
    JASSERT(retval == 0).Text("Restoration of MPI_File_open failed");

    // Update virtual mapping with newly created file
    update_virt_id((mana_mpi_handle){.file = itr->first}, (mana_mpi_handle){.file = fh});

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
      seq_num_init();
      mana_state = RUNNING;

      DmtcpMutexInit(&g_upper_half_fsbase_lock, DMTCP_MUTEX_LLL);
      if (g_upper_half_fsbase != NULL) {
        delete g_upper_half_fsbase;
      }

      g_upper_half_fsbase = new std::unordered_map<pid_t, unsigned long>();
      pid_t real_tid = dmtcp_get_real_tid();
      unsigned long fs = getFS();
      g_upper_half_fsbase->insert(std::make_pair(real_tid, fs));

      if (g_file_flags_map != NULL) {
        delete g_file_flags_map;
      }
      g_file_flags_map = new map<string, int>();

      // TODO(kapil): Remove from final commit.
      setenv(MANA_FILE_REGEX_ENV, ".*", 1);

      if (file_regex == NULL && getenv(MANA_FILE_REGEX_ENV) != NULL) {
        file_regex = new std::regex(getenv(MANA_FILE_REGEX_ENV));
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

      //set global flag to indiacte MANA is initialized
      // FIXME: Move the code from PMPI_Init and PMPI_init_thread wrappers to
      //        This section before setting the Flag for MANA's initialization
      g_libmana_is_initialized = true;
      break;
    }
    case DMTCP_EVENT_EXIT: {
      seq_num_destroy();
      printEventToStderr("EVENT_EXIT");
      JTRACE("*** DMTCP_EVENT_EXIT");
      break;
    }

    case DMTCP_EVENT_OPEN_FD: {
      processFileOpen(data->openFd.path, data->openFd.flags);
      break;
    }

    case DMTCP_EVENT_PTHREAD_START: {
      fprintf(stderr, "PTHREAD_START event triggered\n");
      // Do we need a mutex here?
      DmtcpMutexLock(&g_upper_half_fsbase_lock);
      pid_t real_tid = dmtcp_get_real_tid();
      unsigned long fs = getFS();
      g_upper_half_fsbase->insert(std::make_pair(real_tid, fs));
      DmtcpMutexUnlock(&g_upper_half_fsbase_lock);
      break;
    }

    case DMTCP_EVENT_PTHREAD_RETURN:
    case DMTCP_EVENT_PTHREAD_EXIT: {
      // Do we need a mutex here?
      DmtcpMutexLock(&g_upper_half_fsbase_lock);
      pid_t real_tid = dmtcp_get_real_tid();
      g_upper_half_fsbase->erase(real_tid);
      DmtcpMutexUnlock(&g_upper_half_fsbase_lock);
      break;
    }

    case DMTCP_EVENT_PRESUSPEND: {
      printEventToStderr("EVENT_PRESUSPEND (finish collective op's)");
      mana_state = CKPT_COLLECTIVE;
      // preSuspendBarrier() will send coord response and get worker state.
      // FIXME:  See commant at: dmtcpplugin.cpp:'case DMTCP_EVENT_PRESUSPEND'
      drain_mpi_collective();
      dmtcp_global_barrier("MPI:Drain-Send-Recv");
      mana_state = CKPT_P2P;
      drainSendRecv(); // p2p_drain_send_recv.cpp
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
      // computeUnionOfCkptImageAddresses();
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
      uh_stack_start = lh_info->uh_stack_start;
      uh_stack_end = lh_info->uh_stack_end;
      break;
    }

    case DMTCP_EVENT_RESUME: {
      printEventToStderr("EVENT_RESUME");
      processingOpenCkpFileFds = false;
      dmtcp_local_barrier("MPI:Reset-Drain-Send-Recv-Counters");
      resetDrainCounters(); // p2p_drain_send_recv.cpp
      seq_num_reset();
      dmtcp_local_barrier("MPI:seq_num_reset");
      mana_state = RUNNING;
      printEventToStderr("EVENT_RESUME (done)");
      break;
    }

    case DMTCP_EVENT_RESTART: {
      reset_wrappers();
      initialize_wrappers();
      lh_info->uh_stack_start = uh_stack_start;
      lh_info->uh_stack_end = uh_stack_end;
      printEventToStderr("EVENT_RESTART");
      processingOpenCkpFileFds = false;
      logCkptFileFds();

      mpiInitLhAreas->clear();
      uh_mmaps.clear();

      g_upper_half_fsbase->clear();
      g_upper_half_fsbase->insert(std::make_pair(dmtcp_get_real_tid(), getFS()));

      dmtcp_local_barrier("MPI:updateEnviron");
      seq_num_reset();
      dmtcp_local_barrier("MPI:seq_num_reset");
      init_predefined_virt_ids();
      reconstruct_descriptors();
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
      setCartesianCommunicator(lh_info->getCartesianCommunicatorFptr);
#endif
      dmtcp_global_barrier("MPI:restoreMpiLogState");
      restoreMpiLogState(); // record-replay.cpp
      dmtcp_global_barrier("MPI:record-replay.cpp-void");
      replayMpiP2pOnRestart(); // p2p_log_replay.cpp
      dmtcp_local_barrier("MPI:p2p_log_replay.cpp-void");
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
