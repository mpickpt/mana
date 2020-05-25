#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <link.h>

#include "mtcp_sys.h"
#include "mtcp_util.h"
#include "mtcp_header.h"
#include "mtcp_split_process.h"

int mtcp_sys_errno;
LowerHalfInfo_t info;
MemRange_t *g_lh_mem_range = NULL;

static unsigned long origPhnum;
static unsigned long origPhdr;
static proxyDlsym_t pdlsym;

static void patchAuxv(ElfW(auxv_t) *, unsigned long , unsigned long , int );
static int mmap_iov(const struct iovec *, int );
static int read_proxy_bits(pid_t );
static pid_t startProxy(char *argv0, char **envp);
static int initializeLowerHalf();

int
splitProcess(char *argv0, char **envp)
{
#if 0
  compileProxy();
#endif
  DPRINTF("Initializing Lower-Half Proxy");
  pid_t childpid = startProxy(argv0, envp);
  int ret = -1;
  if (childpid > 0) {
    ret = read_proxy_bits(childpid);
    mtcp_sys_kill(childpid, SIGKILL);
    mtcp_sys_wait4(childpid, NULL, 0, NULL);
  }
  if (ret == 0) {
    ret = initializeLowerHalf();
  }
  return ret;
}

// Local functions
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
  int mtcp_sys_errno;
  void *base = (void *)ROUND_DOWN(iov->iov_base);
  size_t len = ROUND_UP(iov->iov_len);
  int flags =  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
  void *addr = mtcp_sys_mmap(base, len, prot, flags, -1, 0);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("Error mmaping memory: %p, %lu Bytes, %d", base, len,
                mtcp_sys_errno);
    return -1;
  }
  return 0;
}

static int
read_proxy_bits(pid_t childpid)
{
  int ret = -1;
  const int IOV_SZ = 2;
  struct iovec remote_iov[IOV_SZ];
  // text segment
  remote_iov[0].iov_base = info.startTxt;
  remote_iov[0].iov_len = (unsigned long)info.endTxt -
                          (unsigned long)info.startTxt;
  ret = mmap_iov(&remote_iov[0], PROT_READ|PROT_EXEC|PROT_WRITE);
  // data segment
  remote_iov[1].iov_base = info.startData;
  remote_iov[1].iov_len = (unsigned long)info.endOfHeap -
                          (unsigned long)info.startData;
  ret = mmap_iov(&remote_iov[1], PROT_READ|PROT_WRITE);
  // NOTE:  In our case local_iov will be same as remote_iov.
  // NOTE:  This requires same privilege as ptrace_attach (valid for child)
  int i = 0;
  for (i = 0; i < IOV_SZ; i++) {
    DPRINTF("Reading segment from proxy: %p, %lu Bytes\n",
            remote_iov[i].iov_base, remote_iov[i].iov_len);
    ret = mtcp_sys_process_vm_readv(childpid, remote_iov + i /* local_iov */,
                                    1, remote_iov + i, 1, 0);
    MTCP_ASSERT(ret != -1);
    // If the next assert fails, then we had a partial read.
    MTCP_ASSERT(ret == remote_iov[i].iov_len);
  }

  // Can remove PROT_WRITE now that we've oppulated the text segment.
  ret = mtcp_sys_mprotect((void *)ROUND_DOWN(remote_iov[0].iov_base),
                          ROUND_UP(remote_iov[0].iov_len),
                          PROT_READ | PROT_EXEC);
  return ret;
}

// Returns the PID of the proxy child process
static pid_t
startProxy(char *argv0, char **envp)
{
  int pipefd[2] = {0};
  int ret = -1;
  int mtcp_sys_errno;

  if (mtcp_sys_pipe(pipefd) < 0) {
    MTCP_PRINTF("Failed to create pipe: %d", mtcp_sys_errno);
    return ret;
  }

  int childpid = mtcp_sys_fork();
  switch (childpid) {
    case -1:
      MTCP_PRINTF("Failed to fork proxy: %d", mtcp_sys_errno);
      break;

    case 0:
    {
      mtcp_sys_close(pipefd[0]); // close reading end of pipe
      char buf[10];
      mtcp_itoa(pipefd[1], buf);
      char* args[] = {"NO_SUCH_EXECUTABLE",
                            buf,
                            NULL};

      // Replace ".../mtcp_restart" by ".../lh_proxy" in argv0/args[0]
      args[0] = argv0;
      char *last_component = mtcp_strrchr(args[0], '/');
      MTCP_ASSERT(mtcp_strlen("lh_proxy") <= mtcp_strlen(last_component+1));
      mtcp_strcpy(last_component+1, "lh_proxy");

      // FIXME: This is platform-dependent.  The lower half has hardwired
      //        addresses.  They must be changed for each platform.
      mtcp_sys_personality(ADDR_NO_RANDOMIZE);
      MTCP_ASSERT(mtcp_sys_execve(args[0], args, envp) != -1);
      break;
    }

    default:
      // in parent
      mtcp_sys_close(pipefd[1]); // we won't be needing write end of pipe
      if (mtcp_sys_read(pipefd[0], &info, sizeof info) < sizeof info) {
        MTCP_PRINTF("Read fewer bytes than expected");
        break;
      }
      mtcp_sys_close(pipefd[0]);
  }
  return childpid;
}

static void
setLhMemRange()
{
  Area area;

  const uint64_t ONE_GB = 0x40000000;
  const uint64_t TWO_GB = 0x80000000;
  int found = 0;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    DPRINTF("Failed to open proc maps\n");
    return;
  }
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (mtcp_strstr(area.name, "[stack]")) {
      found = 1;
      break;
    }
  }
  mtcp_sys_close(mapsfd);
  if (found && g_lh_mem_range == NULL) {
    g_lh_mem_range = (MemRange_t*)info.memRange;
    g_lh_mem_range->start = (VA)area.addr - TWO_GB;
    g_lh_mem_range->end = (VA)area.addr - ONE_GB;
  }
}

static int
initializeLowerHalf()
{
  int ret = 0;
  int lh_initialized = 0;
  unsigned long argcAddr = (unsigned long)info.parentStackStart;
  resetMmappedList_t resetMmaps = (resetMmappedList_t)info.resetMmappedListFptr;

  // NOTE:
  // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
  // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
  void *stack_end = (void*)(argcAddr - sizeof(unsigned long));
  int argc = *(int*)argcAddr;
  char **argv = (char**)(argcAddr + sizeof(unsigned long));
  char **ev = &argv[argc + 1];
  // char **ev = &((unsigned long*)stack_end[argc + 1]);
  pdlsym = (proxyDlsym_t)info.lh_dlsym;

  // Copied from glibc source
  ElfW(auxv_t) *auxvec;
  {
    char **evp = ev;
    while (*evp++ != NULL);
    auxvec = (ElfW(auxv_t) *) evp;
  }
  setLhMemRange();
  JUMP_TO_LOWER_HALF(info.fsaddr);
  resetMmaps();
  patchAuxv(auxvec, info.lh_AT_PHNUM, info.lh_AT_PHDR, 1);
  getcontext((ucontext_t*)info.g_appContext);

  if (!lh_initialized) {
    lh_initialized = 1;
    libcFptr_t fnc = (libcFptr_t)info.libc_start_main;
    fnc((mainFptr)info.main, argc, argv,
        (mainFptr)info.libc_csu_init,
        (finiFptr)info.libc_csu_fini, 0, stack_end);
  }
  DPRINTF("After getcontext");
  patchAuxv(auxvec, 0, 0, 0);
  RETURN_TO_UPPER_HALF();
  return ret;
}
