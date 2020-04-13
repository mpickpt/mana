// Needed for process_vm_readv
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>


#include "jassert.h"
#include "lower_half_api.h"
#include "mpi_plugin.h"
#include "mpi_nextfunc.h"
#include "mpi_copybits.h"
#include "split_process.h"
#include "procmapsutils.h"
#include "util.h"

static unsigned long origPhnum;
static unsigned long origPhdr;

static unsigned long getStackPtr();
static void patchAuxv(ElfW(auxv_t) *, unsigned long , unsigned long , int );
static int mmap_iov(const struct iovec *, int );
static int read_proxy_bits(pid_t );
static pid_t startProxy();
static int initializeLowerHalf();
static void setLhMemRange();
#if 0
static Area getHighestAreaInMaps();
static char* proxyAddrInApp();
static void compileProxy();
#endif

SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
  this->lowerHalfFs = lowerHalfFs;
  JWARNING(syscall(SYS_arch_prctl, ARCH_GET_FS, &this->upperHalfFs) == 0)
          (JASSERT_ERRNO);
  JWARNING(syscall(SYS_arch_prctl, ARCH_SET_FS, this->lowerHalfFs) == 0)
          (JASSERT_ERRNO);
}

SwitchContext::~SwitchContext()
{
  JWARNING(syscall(SYS_arch_prctl, ARCH_SET_FS, this->upperHalfFs) == 0)
          (JASSERT_ERRNO);
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
    ret = read_proxy_bits(childpid);
    waitpid(childpid, NULL, 0);
  }
  if (ret == 0) {
    ret = initializeLowerHalf();
  }
  return ret;
}

// FIXME: This code is duplicated in proxy and plugin. Refactor into utils.
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

static int
mmap_iov(const struct iovec *iov, int prot)
{
  void *base = (void *)ROUND_DOWN(iov->iov_base);
  size_t len = ROUND_UP(iov->iov_len);
  int flags =  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
  void *ret = mmap(base, len, prot, flags, -1, 0);
  JWARNING(ret != MAP_FAILED)
          (JASSERT_ERRNO)(base)(len)(prot).Text("Error mmaping memory");
  return ret == MAP_FAILED;
}

static int
read_proxy_bits(pid_t childpid)
{
  int ret = -1;
  const int IOV_SZ = 2;
  const int RWX_PERMS = PROT_READ | PROT_EXEC | PROT_WRITE;
  const int RW_PERMS = PROT_READ | PROT_EXEC | PROT_WRITE;
  struct iovec remote_iov[IOV_SZ];

  // text segment
  remote_iov[0].iov_base = info.startTxt;
  remote_iov[0].iov_len = (unsigned long)info.endTxt -
                          (unsigned long)info.startTxt;
  ret = mmap_iov(&remote_iov[0], RWX_PERMS);
  JWARNING(ret == 0)(info.startTxt)(remote_iov[0].iov_len)
          .Text("Error mapping text segment for proxy");
  // data segment
  remote_iov[1].iov_base = info.startData;
  remote_iov[1].iov_len = (unsigned long)info.endOfHeap -
                          (unsigned long)info.startData;
  ret = mmap_iov(&remote_iov[1], RW_PERMS);
  JWARNING(ret == 0)(info.startData)(remote_iov[1].iov_len)
          .Text("Error mapping data segment for proxy");
  // NOTE:  In our case loca_iov will be same as remote_iov.
  // NOTE:  This requires same privilege as ptrace_attach (valid for child)
  for (int i = 0; i < IOV_SZ; i++) {
    JTRACE("Reading segment from proxy")
          (remote_iov[i].iov_base)(remote_iov[i].iov_len);
    ret = process_vm_readv(childpid, remote_iov + i, 1, remote_iov + i, 1, 0);
    JWARNING(ret != -1)(JASSERT_ERRNO).Text("Error reading data from proxy");
  }

  // Can remove PROT_WRITE now that we've oppulated the text segment.
  ret = mprotect((void *)ROUND_DOWN(remote_iov[0].iov_base),
                 ROUND_UP(remote_iov[0].iov_len), PROT_READ | PROT_EXEC);
  return ret;
}

// Returns the PID of the proxy child process
static pid_t
startProxy()
{
  int pipefd[2] = {0};

  if (pipe(pipefd) < 0) {
    JWARNING(false)(JASSERT_ERRNO).Text("Failed to create pipe");
    return -1;
  }

  int childpid = fork();
  switch (childpid) {
    case -1:
      JWARNING(false)(JASSERT_ERRNO).Text("Failed to fork proxy");
      break;

    case 0:
    {
      close(pipefd[0]); // close reading end of pipe
      char buf[10];
      snprintf(buf, sizeof buf, "%d", pipefd[1]); // write end of pipe
      dmtcp::string nockpt = dmtcp::Util::getPath("dmtcp_nocheckpoint");
      dmtcp::string progname = dmtcp::Util::getPath("proxy");
      char* const args[] = {const_cast<char*>(nockpt.c_str()),
                            const_cast<char*>(progname.c_str()),
                            const_cast<char*>(buf),
                            NULL};

      personality(ADDR_NO_RANDOMIZE);
      JWARNING(execvp(args[0], args) != -1)(JASSERT_ERRNO)
        .Text("Failed to exec proxy");
      break;
    }

    default:
      // in parent
      close(pipefd[1]); // we won't be needing write end of pipe
      if (read(pipefd[0], &info, sizeof info) < sizeof info) {
        JWARNING(false)(JASSERT_ERRNO) .Text("Read fewer bytes than expected");
        break;
      }
      close(pipefd[0]);
      break;
  }
  return childpid;
}

static void
setLhMemRange()
{
  const uint64_t ONE_GB = 0x40000000;
  const uint64_t TWO_GB = 0x80000000;
  Area area;
  bool found = false;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    JWARNING(false)(JASSERT_ERRNO).Text("Failed to open proc maps");
    return;
  }
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[stack]")) {
      found = true;
      break;
    }
  }
  close(mapsfd);
  if (found && g_range == NULL) {
    g_range = (MemRange_t*)info.memRange;
    g_range->start = (VA)area.addr - TWO_GB;
    g_range->end = (VA)area.addr - ONE_GB;
  }
}

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
  libcFptr_t fnc = (libcFptr_t)info.libc_start_main;
  pdlsym = (proxyDlsym_t)info.lh_dlsym;
  resetMmappedList_t resetMaps = (resetMmappedList_t)info.resetMmappedListFptr;

  // Copied from glibc source
  ElfW(auxv_t) *auxvec;
  {
    char **evp = ev;
    while (*evp++ != NULL);
    auxvec = (ElfW(auxv_t) *) evp;
  }
  setLhMemRange();
  JUMP_TO_LOWER_HALF(info.fsaddr);
  resetMaps();
  patchAuxv(auxvec, info.lh_AT_PHNUM, info.lh_AT_PHDR, 1);
  JWARNING(getcontext((ucontext_t*)info.g_appContext) == 0)(JASSERT_ERRNO);

  if (!lh_initialized) {
    lh_initialized = true;
    fnc((mainFptr)info.main, argc, argv,
        (mainFptr)info.libc_csu_init,
        (finiFptr)info.libc_csu_fini, 0, stack_end);
  }
  JTRACE("After getcontext");
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
  dmtcp::string cmd = "make proxy PROXY_TXT_ADDR=0x";
  dmtcp::string safe_addr = proxyAddrInApp();
  cmd.append(safe_addr);
  int ret = system(cmd.c_str());
  if (ret == -1) {
      ret = system("make proxy");
      if (ret == -1) {
        JWARNING(false)(JASSERT_ERRNO).Text("Proxy building failed!");
      }
  }
}
#endif
