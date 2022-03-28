/*****************************************************************************
 * Copyright (C) 2014 Kapil Arya <kapil@ccs.neu.edu>                         *
 * Copyright (C) 2014 Gene Cooperman <gene@ccs.neu.edu>                      *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

/* Algorithm:
 *    When DMTCP originally launched, it mmap'ed a region of memory sufficient
 *  to hold the mtcp_restart executable.  This mtcp_restart was compiled
 *  as position-independent code (-fPIC).  So, we can copy the code into
 *  the memory (holebase) that was reserved for us during DMTCP launch.
 *    When we move to high memory, we will also use a temporary stack
 *  within the reserved memory (holebase).  Changing from the original
 *  mtcp_restart stack to a new stack must be done with care.  All information
 *  to be passed will be stored in the variable rinfo, a global struct.
 *  When we enter the copy of restorememoryareas() in the reserved memory,
 *  we will copy the data of rinfo from the global rinfo data to our
 *  new call frame.
 *    It is then safe to munmap the old text, data, and stack segments.
 *  Once we have done the munmap, we have almost finished bootstrapping
 *  ourselves.  We only need to copy the memory sections from the checkpoint
 *  image to the original addresses in memory.  Finally, we will then jump
 *  back using the old program counter of the checkpoint thread.
 *    Now, the copy of mtcp_restart is "dead code", and we will not use
 *  this memory region again until we restart from the next checkpoint.
 */

#define _GNU_SOURCE 1
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/version.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>

#include "../membarrier.h"
#include "config.h"
#include "mtcp_check_vdso.ic"
#include "mtcp_header.h"
#include "mtcp_sys.h"
#include "mtcp_util.ic"
#include "procmapsarea.h"
#include "tlsutil.h"
#include "mtcp_split_process.h"

#define HUGEPAGES

/* The use of NO_OPTIMIZE is deprecated and will be removed, since we
 * compile mtcp_restart.c with the -O0 flag already.
 */
#ifdef __clang__
# define NO_OPTIMIZE __attribute__((optnone)) /* Supported only in late 2014 */
#else /* ifdef __clang__ */
# define NO_OPTIMIZE __attribute__((optimize(0)))
#endif /* ifdef __clang__ */

void mtcp_check_vdso(char **environ);
#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags);
#endif

#define BINARY_NAME     "mtcp_restart"
#define BINARY_NAME_M32 "mtcp_restart-32"

/* struct RestoreInfo to pass all parameters from one function to next.
 * This must be global (not on stack) at the time that we jump from
 * original stack to copy of restorememoryareas() on new stack.
 * This is because we will wait until we are in the new call frame and then
 * copy the global data into the new call frame.
 */
#define STACKSIZE 4 * 1024 * 1024

// static long long tempstack[STACKSIZE];
RestoreInfo rinfo;

/* Internal routines */
static void readmemoryareas(int fd, VA endOfStack);
static int read_one_memory_area(int fd, VA endOfStack);
#if 0
static void adjust_for_smaller_file_size(Area *area, int fd);
#endif /* if 0 */
static void restorememoryareas(RestoreInfo *rinfo_ptr,
                               LowerHalfInfo_t *linfo_ptr);
static void restore_brk(VA saved_brk, VA restore_begin, VA restore_end);
static void restart_fast_path(void);
static void restart_slow_path(void);
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2);
static int hasOverlappingMapping(VA addr, size_t size);
static int mremap_move(void *dest, void *src, size_t size);
static void getTextAddr(VA *textAddr, size_t *size);
static void mtcp_simulateread(int fd, MtcpHeader *mtcpHdr);
void restore_libc(ThreadTLSInfo *tlsInfo,
                  int tls_pid_offset,
                  int tls_tid_offset,
                  MYINFO_GS_T myinfo_gs);
static void unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo,
                                                LowerHalfInfo_t *lh_info);
// This emulates MAP_FIXED_NOREPLACE, which became available only in Linux 4.17
// FIXME:  This assume that addr is a multiple of PAGESIZE.  We should
//         check that in the function, and either issue an error in that
//         case, or else simulate the action of MAP_FIXED_NOREPLACE.
void* mmap_fixed_noreplace(void *addr, size_t len, int prot, int flags,
                         int fd, off_t offset) {
  if (flags & MAP_FIXED) {
    flags ^= MAP_FIXED;
  }
  void *addr2 = mtcp_sys_mmap(addr, len, prot, flags, fd, offset);
  if (addr == addr2) {
    return addr2;
  } else {
    mtcp_sys_munmap(addr2, len);
    return MAP_FAILED;
  }
}


#define MB                 1024 * 1024
#define RESTORE_STACK_SIZE 5 * MB
#define RESTORE_MEM_SIZE   5 * MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE + RESTORE_MEM_SIZE)

// const char service_interp[] __attribute__((section(".interp"))) =
// "/lib64/ld-linux-x86-64.so.2";

int
__libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                             int argc,
                             char **argv,
                             __typeof (main) init,
                             void (*fini) (void),
                             void (*rtld_fini) (void),
                             void *stack_end)
{
  int mtcp_sys_errno;
  char **envp = argv + argc + 1;
  int result = main(argc, argv, envp);

  mtcp_sys_exit(result);
  (void)mtcp_sys_errno; /* Stop compiler warning about unused variable */
  while (1) {}
}


int
__libc_csu_init(int argc, char **argv, char **envp)
{
  return 0;
}

void
__libc_csu_fini(void) {}

void __stack_chk_fail(void);   /* defined at end of file */
void
abort(void) { mtcp_abort(); }

/* Implement memcpy() and memset() inside mtcp_restart. Although we are not
 * calling memset, the compiler may generate a call to memset() when trying to
 * initialize a large array etc.
 */
void *
memset(void *s, int c, size_t n)
{
  return mtcp_memset(s, c, n);
}

void *
memcpy(void *dest, const void *src, size_t n)
{
  return mtcp_memcpy(dest, src, n);
}

NO_OPTIMIZE
char*
getCkptImageByRank(int rank, char **argv)
{
  char *fname = NULL;
  if (rank >= 0) {
    fname = argv[rank];
  }
  return fname;
}

static inline int
regionContains(const void *haystackStart,
               const void *haystackEnd,
               const void *needleStart,
               const void *needleEnd)
{
  return needleStart >= haystackStart && needleEnd <= haystackEnd;
}

// We reserve these memory regions even if some OS's do not have drivers (such
// as CentOS). On Cori, these regions are used for GNI drivers.
static void
reserveUpperHalfMemoryRegionsForCkptImgs(char *start1, char *end1,
	                                 char *start2, char *end2)
{
  // FIXME: This needs to be made dynamic.
  const size_t len1 = end1 - start1;
  const size_t len2 = end2 - start2;

  void *addr = mmap_fixed_noreplace(start1, len1,
                             PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                             -1, 0);
  MTCP_ASSERT(addr == start1);
  if (len2 > 0) {
    addr = mmap_fixed_noreplace(start2, len2,
                         PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         -1, 0);
    MTCP_ASSERT(addr == start2);
  }
}

static void
releaseUpperHalfMemoryRegionsForCkptImgs(char *start1, char *end1,
                                           char *start2, char *end2)
{
  // FIXME: This needs to be made dynamic.
  const size_t len1 = end1 - start1;
  const size_t len2 = end2 - start2;

  mtcp_sys_munmap(start1, len1);
  if (len2 > 0) {
    mtcp_sys_munmap(start2, len2);
  }
}

int reserve_fds_upper_half(int *reserved_fds) {
  int total_reserved_fds = 0;
  int tmp_fd = 0;
    while (tmp_fd < 500) {
      MTCP_ASSERT((tmp_fd = mtcp_sys_open2("/dev/null",O_RDONLY)) != -1);  
      reserved_fds[total_reserved_fds]=tmp_fd;
      total_reserved_fds++;
    }
  return total_reserved_fds;
}

void unreserve_fds_upper_half(int *reserved_fds, int total_reserved_fds) {
    while (--total_reserved_fds >= 0) {
      MTCP_ASSERT((mtcp_sys_close(reserved_fds[total_reserved_fds])) != -1);  
    }
}

int itoa2(int value, char* result, int base) {
	// check that the base if valid
	if (base < 2 || base > 36) { *result = '\0'; return 0; }

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	int len = 0;
	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
		len++;
	} while ( value );

	// Apply negative sign
	if (tmp_value < 0) *ptr++ = '-';
	*ptr-- = '\0';
	while(ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--= *ptr1;
		*ptr1++ = tmp_char;
	}
	return len;
}

int atoi2(char* str) 
{ 
	// Initialize result 
	int res = 0; 

	// Iterate through all characters 
	// of input string and update result 
	int i;
	for (i = 0; str[i] 
			!= '\0'; 
			++i) 
		res = res * 10 + str[i] - '0'; 

	// return result. 
	return res; 
}

int my_memcmp(const void *buffer1, const void *buffer2, size_t len) {
  const uint8_t *bbuf1 = (const uint8_t *) buffer1;
  const uint8_t *bbuf2 = (const uint8_t *) buffer2;
  size_t i;
  for (i = 0; i < len; ++i) {
      if(bbuf1[i] != bbuf2[i]) return bbuf1[i] - bbuf2[i];
  }
  return 0;
}

// FIXME: Many style rules broken.  Code never reviewed by skilled programmer.
int getCkptImageByDir(char *buffer, size_t buflen, int rank) {
  if(!rinfo.restart_dir) {
    MTCP_PRINTF("***ERROR No restart directory found - cannot find checkpoint image by directory!");
    return -1;
  }

  size_t len = mtcp_strlen(rinfo.restart_dir);
  if(len >= buflen){
    MTCP_PRINTF("***ERROR Restart directory would overflow given buffer!");
    return -1;
  }
  mtcp_strcpy(buffer, rinfo.restart_dir); // start with directory

  // ensure directory ends with /
  if(buffer[len - 1] != '/') {
    if(len + 2 > buflen){ // Make room for buffer(strlen:len) + '/' + '\0'
      MTCP_PRINTF("***ERROR Restart directory would overflow given buffer!");
      return -1;
    }
    buffer[len] = '/';
    buffer[len+1] = '\0';
    len += 1;
  }

  if(len + 10 >= buflen){
    MTCP_PRINTF("***ERROR Ckpt directory would overflow given buffer!");
    return -1;
  }
  mtcp_strcpy(buffer + len, "ckpt_rank_");
  len += 10; // length of "ckpt_rank_"

  // "Add rank"
  len += itoa2(rank, buffer + len, 10); // TODO: this can theoretically overflow
  if(len + 10 >= buflen){
    MTCP_PRINTF("***ERROR Ckpt directory has overflowed the given buffer!");
    return -1;
  }

  // append '/'
  if(len + 1 >= buflen){
    MTCP_PRINTF("***ERROR Ckpt directory would overflow given buffer!");
    return -1;
  }
  buffer[len] = '/';
  buffer[len + 1] = '\0'; // keep null terminated for open call
  len += 1;

  int fd = mtcp_sys_open2(buffer, O_RDONLY | O_DIRECTORY);
  if(fd == -1) {
      return -1;
  }

  char ldirents[256];
  int found = 0;
  while(!found){
      int nread = mtcp_sys_getdents(fd, ldirents, 256);
      if(nread == -1) {
          MTCP_PRINTF("***ERROR reading directory entries from directory (%s); errno: %d\n",
                      buffer, mtcp_sys_errno);
          return -1;
      }
      if(nread == 0) return -1; // end of directory

      int bpos = 0;
      while(bpos < nread) {
        struct linux_dirent *entry = (struct linux_dirent *) (ldirents + bpos);
        int slen = mtcp_strlen(entry->d_name);
        // int slen = entry->d_reclen - 2 - offsetof(struct linux_dirent, d_name);
        if(slen > 6 
            && my_memcmp(entry->d_name, "ckpt", 4) == 0
            && my_memcmp(entry->d_name + slen - 6, ".dmtcp", 6) == 0) {
          found = 1;
          if(len + slen >= buflen){
            MTCP_PRINTF("***ERROR Ckpt file name would overflow given buffer!");
            len = -1;
            break; // don't return or we won't close the file
          }
          mtcp_strcpy(buffer + len, entry->d_name);
          len += slen;
          break;
        }

        if(entry->d_reclen == 0) {
          MTCP_PRINTF("***ERROR Directory Entry struct invalid size of 0!");
          found = 1; // just to exit outer loop
          len = -1;
          break; // don't return or we won't close the file
        }
        bpos += entry->d_reclen;
      }
  }

  if(mtcp_sys_close(fd) == -1) {
      MTCP_PRINTF("***ERROR closing ckpt directory (%s); errno: %d\n",
                  buffer, mtcp_sys_errno);
      return -1;
  }
  
  return len;
}

#define min(a,b) (a < b ? a : b)
#define max(a,b) (a > b ? a : b)
int discover_union_ckpt_images(char *argv[],
	                       char **libsStart, char **libsEnd,
	                       char **highMemStart) {
  MtcpHeader mtcpHdr;
  int rank;
  *libsStart = (void *)(-1); // We'll take a min later.
  *libsEnd = NULL; // We'll take a max later.
  *highMemStart = (void *)(-1); // We'll take a min later.
  for (rank = 0; ; rank++) {
    char ckptImageFull[512]; // TODO: is this a big enough buffer?
    char *ckptImage = NULL;

    if(rinfo.restart_dir && getCkptImageByDir(ckptImageFull, 512, rank) != -1) {
      ckptImage = ckptImageFull;
    } else if(!rinfo.restart_dir){
      ckptImage = getCkptImageByRank(rank, argv);
    }
    if (ckptImage == NULL) {
        break;
    }
    // FIXME: This code is duplicated from below.  Refactor it.
    int rc = -1;
    int fd = mtcp_sys_open2(ckptImage, O_RDONLY);
    if (fd == -1) {
      MTCP_PRINTF("***ERROR opening ckpt image (%s); errno: %d\n",
                  ckptImage, mtcp_sys_errno);
      break;
    }
    do {
      rc = mtcp_readfile(fd, &mtcpHdr, sizeof mtcpHdr);
    } while (rc > 0 && mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) != 0);
    if (rc == 0) { // if end of file
      MTCP_PRINTF("***ERROR: ckpt image doesn't match MTCP_SIGNATURE\n");
      return -1;  // exit with error code -1
    }
    if(mtcp_sys_close(fd) == -1) {
      MTCP_PRINTF("***ERROR closing ckpt image (%s); errno: %d\n",
                  ckptImage, mtcp_sys_errno);
      break;
    }
    *libsStart = min(*libsStart, (char *)mtcpHdr.libsStart);
    *libsEnd = max(*libsEnd, (char *)mtcpHdr.libsEnd);
    *highMemStart = min(*highMemStart, (char *)mtcpHdr.highMemStart);
  }
  return rank;
}

NO_OPTIMIZE
static unsigned long int
mygetauxval(char **evp, unsigned long int type)
{ while (*evp++ != NULL) {};
  Elf64_auxv_t *auxv = (Elf64_auxv_t *)evp;
  Elf64_auxv_t *p;
  for (p = auxv; p->a_type != AT_NULL; p++) {
    if (p->a_type == type)
       return p->a_un.a_val;
  }
  return 0;
}

NO_OPTIMIZE
static int
mysetauxval(char **evp, unsigned long int type, unsigned long int val)
{ while (*evp++ != NULL) {};
  Elf64_auxv_t *auxv = (Elf64_auxv_t *)evp;
  Elf64_auxv_t *p;
  for (p = auxv; p->a_type != AT_NULL; p++) {
    if (p->a_type == type) {
       p->a_un.a_val = val;
       return 0;
    }
  }
  return -1;
}

/* We hardwire these, since we can't make libc calls. */
#define PAGESIZE 4096
#define ROUNDADDRUP(addr, size) ((addr + size - 1) & ~(size - 1))

// This function searches for a free memory region to temporarily remap the vdso
// and vvar regions to, so that mtcp's vdso and vvar do not overlap with the
// checkpointed process' vdso and vvar.
static void
remap_vdso_and_vvar_regions(RestoreInfo *rinfo) {
  Area area;
  void *rc = 0;
  uint64_t tmpVvarStart = 0;
  uint64_t tmpVdsoStart = 0;
  size_t vvarSize = 0;
  size_t vdsoSize = 0;
  uint64_t prev_addr = 0x10000;

  rinfo->currentVdsoStart = NULL;
  rinfo->currentVdsoEnd = NULL;
  rinfo->currentVvarStart = NULL;
  rinfo->currentVvarEnd = NULL;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (mtcp_strcmp(area.name, "[vvar]") == 0) {
      rinfo->currentVvarStart = area.addr;
      rinfo->currentVvarEnd = area.endAddr;
      vvarSize = area.size;
    } else if (mtcp_strcmp(area.name, "[vdso]") == 0) {
      rinfo->currentVdsoStart = area.addr;
      rinfo->currentVdsoEnd = area.endAddr;
      vdsoSize = area.size;
    }

    if (vvarSize > 0 && vdsoSize > 0) {
      break;
    }
  }

  mtcp_sys_lseek(mapsfd, 0, SEEK_SET);

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (prev_addr + vvarSize + vdsoSize <= area.__addr) {
      if (vvarSize > 0) {
        /* some kernel doesn't have vvar */
        tmpVvarStart = prev_addr;
      }
      /* Assuming vdso always comes right after vvar */
      tmpVdsoStart = prev_addr + vvarSize;
      break;
    } else {
      prev_addr = ROUNDADDRUP((uint64_t) area.endAddr, PAGESIZE);
    }
  }

  mtcp_sys_close(mapsfd);

  if (vvarSize > 0) {
    if (tmpVvarStart == 0) {
      MTCP_PRINTF("No free region found to temporarily map vvar to\n");
      mtcp_abort();
    }
    rc = mtcp_sys_mremap(rinfo->currentVvarStart, vvarSize, vvarSize,
                         MREMAP_FIXED | MREMAP_MAYMOVE, (void *)tmpVvarStart);
    if (rc == MAP_FAILED) {
      MTCP_PRINTF("mtcp_restart failed: "
                  "gdb --mpi \"DMTCP_ROOT/bin/mtcp_restart\" to debug.\n");
      mtcp_abort();
    }
    rinfo->currentVvarStart = (VA)tmpVvarStart;
    rinfo->currentVvarEnd = (VA)(tmpVvarStart + vvarSize);
  }

  if (vdsoSize > 0) {
    if (tmpVdsoStart == 0) {
      MTCP_PRINTF("No free region found to temporarily map vdso to\n");
      mtcp_abort();
    }
    rc = mtcp_sys_mremap(rinfo->currentVdsoStart, vdsoSize, vdsoSize,
                         MREMAP_FIXED | MREMAP_MAYMOVE, (void *)tmpVdsoStart);
    if (rc == MAP_FAILED) {
      MTCP_PRINTF("mtcp_restart failed: "
                  "gdb --mpi \"DMTCP_ROOT/bin/mtcp_restart\" to debug.\n");
      mtcp_abort();
    }
    rinfo->currentVdsoStart = (VA)tmpVdsoStart;
    rinfo->currentVdsoEnd = (VA)(tmpVdsoStart + vdsoSize);
  }
}

#define shift argv++; argc--;
NO_OPTIMIZE
int
main(int argc, char *argv[], char **environ)
{
  char *ckptImage = NULL;
  char ckptImageNew[512];
  MtcpHeader mtcpHdr;
  int mtcp_sys_errno;
  int simulate = 0;
  int runMpiProxy = 0;
  char *argv0 = argv[0]; // Needed for runMpiProxy

  if (argc == 1) {
    MTCP_PRINTF("***ERROR: This program should not be used directly.\n");
    mtcp_sys_exit(1);
  }

#if 0
  MTCP_PRINTF("Attach for debugging.");
  { int x = 1; while (x) {}
  }
#endif /* if 0 */

  // TODO(karya0): Remove vDSO checks after 2.4.0-rc3 release, and after
  // testing.
  // Without mtcp_check_vdso, CentOS 7 fails on dmtcp3, dmtcp5, others.
#define ENABLE_VDSO_CHECK

  // TODO(karya0): Remove this block and the corresponding file after sufficient
  // testing:  including testing for __i386__, __arm__ and __aarch64__
#ifdef ENABLE_VDSO_CHECK

  /* i386 uses random addresses for vdso.  Make sure that its location
   * will not conflict with other memory regions.
   * (Other arch's may also need this in the future.  So, we do it for all.)
   * Note that we may need to keep the old and the new vdso.  We may
   * have checkpointed inside gettimeofday inside the old vdso, and the
   * kernel, on restart, knows only the new vdso.
   */
  mtcp_check_vdso(environ);
#endif /* ifdef ENABLE_VDSO_CHECK */

  rinfo.fd = -1;
  rinfo.mtcp_restart_pause = 0; /* false */
  rinfo.use_gdb = 0;
  rinfo.text_offset = -1;
  rinfo.restart_dir = NULL;
  shift;
  while (argc > 0) {
    if (mtcp_strcmp(argv[0], "--use-gdb") == 0) {
      rinfo.use_gdb = 1;
      shift;
    } else if (mtcp_strcmp(argv[0], "--mpi") == 0) {
      runMpiProxy = 1;
      shift;
    } else if (mtcp_strcmp(argv[0], "--text-offset") == 0) {
      rinfo.text_offset = mtcp_strtol(argv[1]);
      shift; shift;

      // Flags for call by dmtcp_restart follow here:
    } else if (mtcp_strcmp(argv[0], "--fd") == 0) {
      rinfo.fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--stderr-fd") == 0) {
      rinfo.stderr_fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--mtcp-restart-pause") == 0) {
      rinfo.mtcp_restart_pause = argv[1][0] - '0'; /* true */
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--simulate") == 0) {
      simulate = 1;
      shift;
    } else if (mtcp_strcmp(argv[0], "--restartdir") == 0) {
      rinfo.restart_dir = argv[1];
      shift; shift;
    } else if (argc == 1) {
      // We would use MTCP_PRINTF, but it's also for output of util/readdmtcp.sh
      mtcp_printf("Considering '%s' as a ckpt image.\n", argv[0]);
      ckptImage = argv[0];
      break;
    } else if (runMpiProxy) {
      // N.B.: The assumption here is that the user provides the `--mpi` flag
      // followed by a list of checkpoint images
      break;
    } else {
      MTCP_PRINTF("MTCP Internal Error\n");
      return -1;
    }
  }

  if ((rinfo.fd != -1) ^ (ckptImage == NULL) && !runMpiProxy) {
    MTCP_PRINTF("***MTCP Internal Error\n");
    mtcp_abort();
  }

  if (runMpiProxy) {
    // USAGE:  DMTCP_MANA_PAUSE=1 srun -n XXX gdb --args dmtcp_restart ...
    //   (for DEBUGGING by setting DMTCP_MANA_PAUSE environment variable)
    // RATIONALE: When using Slurm's srun, dmtcp_restart is a child process
    //   of slurmstepd.  So, 'gdb --args srun -n XX dmtcp_restart ...' doesn't
    //   work, since 'dmtcp_restart ...' is fork/exec'ed from slurmstepd.
    // ALTERNATIVE (tested under Slurm 19.05):
    //   'srun --input all' should "broadcast stdin to all remote tasks".
    //   But 'srun --input all -n 1 gdb --args dmtcp_restart ...'
    //   fails.  GDB starts up, but then exits (due to end-of-stdin??).
    char **myenvp = environ;
    while (*myenvp != NULL) {
      if (mtcp_strstartswith(*myenvp++, "DMTCP_MANA_PAUSE=")) {
        MTCP_PRINTF("*** PAUSED FOR DEBUGGING: Please do:\n  *** gdb %s %d\n\n",
                    argv0, mtcp_sys_getpid());
        MTCP_PRINTF("***     (OR, for one rank only, do:)\n"
                    "  ***     gdb %s `pgrep -n mtcp_restart`\n\n", argv0);
        MTCP_PRINTF("*** Then do '(gdb) p dummy=0' to continue debugging.\n\n");
        volatile int dummy = 1;
        while (dummy) {};
        break;
      }
    }

    // We could have read /proc/self/maps for vdsoStart, etc.  But that code
    //   is long, and is used to discover many things about the maps.
    //   By using mygetauxval(), we have a much shorter mechanism now.
    // If we want to test this, we can add code to do a trial mremap with a page
    //   before vvar and after vdso, and verify that we get an EFAULT.
    // In May, 2020, on Cori and elsewhere, vvar is 3 pages and vdso is 2 pages.
    char *vdsoStart = (char *)mygetauxval(environ, AT_SYSINFO_EHDR);
    DPRINTF("vdsoStart: %p\n", vdsoStart);

    /* FIXME: 1. We used to use `mygetauxval` to get VdsoStart, now seems we have to
     * parse the proc map, shall we remove the lines above?
     * 2. Is this only needed for MANA? I think this could be a general routine which
     * dmtcp also need to do it
     * 3. Can we check and only do it when there is an overlap? I think we don't have
     * to do it if there was no overlap */
    remap_vdso_and_vvar_regions(&rinfo);

    // Now that we moved vdso/vvar, we need to update the vdso address
    //   in the auxv vector.  This will be needed when the lower half
    //   starts up:  initializeLowerHalf() will be called from splitProcess().
    mysetauxval(environ, AT_SYSINFO_EHDR, (uint64_t)rinfo.currentVdsoStart);
  }

  if (runMpiProxy) {
    // NOTE: We use mtcp_restart's original stack to initialize the lower
    // half. We need to do this in order to call MPI_Init() in the lower half,
    // which is required to figure out our rank, and hence, figure out which
    // checkpoint image to open for memory restoration.
    // The other assumption here is that we can only handle uncompressed
    // checkpoint images.

    // This creates the lower half and copies the bits to this address space
    splitProcess(argv0, environ);

    char *libsStart;   // used by MPI: start of where kernel mmap's libraries
    char *libsEnd;     // used by MPI: end of where kernel mmap's libraries
    char *highMemStart;// used by MPI: start of memory beyond libraries
    discover_union_ckpt_images(argv, &libsStart, &libsEnd, &highMemStart);
    
    // Reserve first 500 file descriptors for the Upper-half
    int reserved_fds[500];
    int total_reserved_fds;
    total_reserved_fds = reserve_fds_upper_half(reserved_fds);

    // Refer to "blocked memory" in MANA Plugin Documentation for the addresses
# define GB (uint64_t)(1024 * 1024 * 1024)
    // FIXME:  Rewrite this logic more carefully.
    char *start1, *start2, *end1, *end2;
    if (libsEnd + 1 * GB < highMemStart /* end of stack of upper half */) {
      start1 = libsStart;    // first lib (ld.so) of upper half
      // lh_info.memRange is the memory region for the mmaps of the lower half.
      // Apparently lh_info.memRange is a region between 1 GB and 2 GB below
      //   the end of stack in the lower half.
      // One gigabyte below those mmaps of the lower half, we are creating space
      //   for the GNI driver data, to be created when the GNI library runs.
      // This says that we have to reserve only up to the mtcp_restart stack.
      end1 = lh_info.memRange.start - 1 * GB;
      // start2 == end2:  So, this will not be used.
      start2 = 0;
      end2 = start2;
    } else {
      // On standard Ubuntu/CentOS libs are mmap'ed downward in memory.
      // Allow an extra 1 GB for future upper-half libs and mmaps to be loaded.
      // FIXME:  Will the GNI driver data be allocated below start1?
      //         If so, fix this to block more than a 1 GB address range.
      //         The GNI driver data is only used by Cray GNI.
      //         But lower-half MPI_Init may call additional mmap's not
      //           controlled by us.
      //         Check this logic.
      // NOTE:   setLhMemRange (lh_info.memRange: future mmap's of lower half)
      //           was chosen to be in safe memory region
      // NOTE:   When we mmap start1..end1, we will overwrite the text and
      //         data segments of ld.so belonging to mtcp_restart.  But
      //         mtcp_restart is statically linked, and doesn't need it.
      Area heap_area;
      MTCP_ASSERT(getMappedArea(&heap_area, "[heap]") == 1);
      start1 = max(heap_area.endAddr, lh_info.memRange.end);
      Area stack_area;
      MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
      end1 = min(stack_area.endAddr - 4 * GB, highMemStart - 4 * GB);
      start2 = 0;
      end2 = start2;
    }
    typedef int (*getRankFptr_t)(void);
    int rank = -1;
    reserveUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    
    // MPI_Init is called here. GNI memory areas will be loaded by MPI_Init.
    rank = ((getRankFptr_t)lh_info.getRankFptr)();
    RETURN_TO_UPPER_HALF();
    releaseUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
    unreserve_fds_upper_half(reserved_fds,total_reserved_fds);

    if(getCkptImageByDir(ckptImageNew, 512, rank) != -1) {
        ckptImage = ckptImageNew;
    } else {
        ckptImage = getCkptImageByRank(rank, argv);
    }
    MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, ckptImage);
    //ckptImage = getCkptImageByRank(rank, argv);
    //MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, ckptImage);
  }

#ifdef TIMING
  mtcp_sys_gettimeofday(&rinfo.startValue, NULL);
#endif
  if (rinfo.fd != -1) {
    mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
  } else {
    int rc = -1;
    rinfo.fd = mtcp_sys_open2(ckptImage, O_RDONLY);
    if (rinfo.fd == -1) {
      MTCP_PRINTF("***ERROR opening ckpt image (%s); errno: %d\n",
                  ckptImage, mtcp_sys_errno);
      mtcp_abort();
    }

    // This assumes that the MTCP header signature is unique.
    // We repeatedly look for mtcpHdr because the first header will be
    // for DMTCP.  So, we look deeper for the MTCP header.  The MTCP
    // header is guaranteed to start on an offset that's an integer
    // multiple of sizeof(mtcpHdr), which is currently 4096 bytes.
    do {
      rc = mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
    } while (rc > 0 && mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) != 0);
    if (rc == 0) { /* if end of file */
      MTCP_PRINTF("***ERROR: ckpt image doesn't match MTCP_SIGNATURE\n");
      return 1;  /* exit with error code 1 */
    }
  }

  DPRINTF("For debugging:\n"
          "    (gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
          mtcpHdr.restore_addr + rinfo.text_offset);
  if (rinfo.text_offset == -1) {
    DPRINTF("... but add to the above the result, 1 +"
            " `text_offset.sh mtcp_restart`\n    in the mtcp subdirectory.\n");
  }

  if (simulate) {
    mtcp_simulateread(rinfo.fd, &mtcpHdr);
    return 0;
  }

  rinfo.saved_brk = mtcpHdr.saved_brk;
  rinfo.restore_addr = mtcpHdr.restore_addr;
  rinfo.restore_end = mtcpHdr.restore_addr + mtcpHdr.restore_size;
  rinfo.restore_size = mtcpHdr.restore_size;
  rinfo.vdsoStart = mtcpHdr.vdsoStart;
  rinfo.vdsoEnd = mtcpHdr.vdsoEnd;
  rinfo.vvarStart = mtcpHdr.vvarStart;
  rinfo.vvarEnd = mtcpHdr.vvarEnd;
  rinfo.endOfStack = mtcpHdr.end_of_stack;
  rinfo.post_restart = mtcpHdr.post_restart;
  rinfo.post_restart_debug = mtcpHdr.post_restart_debug;
  rinfo.motherofall_tls_info = mtcpHdr.motherofall_tls_info;
  rinfo.tls_pid_offset = mtcpHdr.tls_pid_offset;
  rinfo.tls_tid_offset = mtcpHdr.tls_tid_offset;
  rinfo.myinfo_gs = mtcpHdr.myinfo_gs;

  restore_brk(rinfo.saved_brk, rinfo.restore_addr,
              rinfo.restore_addr + rinfo.restore_size);
  getTextAddr(&rinfo.text_addr, &rinfo.text_size);
  if (hasOverlappingMapping(rinfo.restore_addr, rinfo.restore_size)) {
    MTCP_PRINTF("*** Not Implemented.\n\n");
    mtcp_abort();
    restart_slow_path();
  } else {
    restart_fast_path();
  }
  return 0;  /* Will not reach here, but need to satisfy the compiler */
}

NO_OPTIMIZE
static void
restore_brk(VA saved_brk, VA restore_begin, VA restore_end)
{
  int mtcp_sys_errno;
  VA current_brk;
  VA new_brk;

  /* The kernel (2.6.9 anyway) has a variable mm->brk that we should restore.
   * The only access we have is brk() which basically sets mm->brk to the new
   * value, but also has a nasty side-effect (as far as we're concerned) of
   * mmapping an anonymous section between the old value of mm->brk and the
   * value being passed to brk().  It will munmap the bracketed memory if the
   * value being passed is lower than the old value.  But if zero, it will
   * return the current mm->brk value.
   *
   * So we're going to restore the brk here.  As long as the current mm->brk
   * value is below the static restore region, we're ok because we 'know' the
   * restored brk can't be in the static restore region, and we don't care if
   * the kernel mmaps something or munmaps something because we're going to wipe
   * it all out anyway.
   */

  current_brk = mtcp_sys_brk(NULL);
  if ((current_brk > restore_begin) &&
      (saved_brk < restore_end)) {
    MTCP_PRINTF("current_brk %p, saved_brk %p, restore_begin %p,"
                " restore_end %p\n",
                current_brk, saved_brk, restore_begin,
                restore_end);
    mtcp_abort();
  }

  if (current_brk <= saved_brk) {
    new_brk = mtcp_sys_brk(saved_brk);
    rinfo.saved_brk = NULL; // We no longer need the value of saved_brk.
  } else {
    new_brk = saved_brk;

    // If saved_brk < current_brk, then brk() does munmap; we can lose rinfo.
    // So, keep the value rinfo.saved_brk, and call mtcp_sys_brk() later.
    return;
  }
  if (new_brk == (VA)-1) {
    MTCP_PRINTF("sbrk(%p): errno: %d (bad heap)\n",
                saved_brk, mtcp_sys_errno);
    mtcp_abort();
  } else if (new_brk > current_brk) {
    // Now unmap the just mapped extended heap. This is to ensure that we don't
    // have overlap with the restore region.
    if (mtcp_sys_munmap(current_brk, new_brk - current_brk) == -1) {
      MTCP_PRINTF("***WARNING: munmap failed; errno: %d\n", mtcp_sys_errno);
    }
  }
  if (new_brk != saved_brk) {
    if (new_brk == current_brk && new_brk > saved_brk) {
      DPRINTF("new_brk == current_brk == %p\n; saved_break, %p,"
              " is strictly smaller;\n  data segment not extended.\n",
              new_brk, saved_brk);
    } else {
      if (new_brk == current_brk) {
        MTCP_PRINTF("warning: new/current break (%p) != saved break (%p)\n",
                    current_brk, saved_brk);
      } else {
        MTCP_PRINTF("error: new break (%p) != current break (%p)\n",
                    new_brk, current_brk);
      }

      // mtcp_abort ();
    }
  }
}

#ifdef __aarch64__
// From Dynamo RIO, file:  dr_helper.c
// See https://github.com/DynamoRIO/dynamorio/wiki/AArch64-Port
//   (self-modifying code) for background.
# define ALIGN_FORWARD(addr,align) (void *)(((unsigned long)addr + align - 1) & ~(unsigned long)(align-1))
# define ALIGN_BACKWARD(addr,align) (void *)((unsigned long)addr & ~(unsigned long)(align-1))
void
clear_icache(void *beg, void *end)
{
    static size_t cache_info = 0;
    size_t dcache_line_size;
    size_t icache_line_size;
    typedef unsigned int* ptr_uint_t;
    ptr_uint_t beg_uint = (ptr_uint_t)beg;
    ptr_uint_t end_uint = (ptr_uint_t)end;
    ptr_uint_t addr;

    if (beg_uint >= end_uint)
        return;

    /* "Cache Type Register" contains:
     * CTR_EL0 [31]    : 1
     * CTR_EL0 [19:16] : Log2 of number of 4-byte words in smallest dcache line
     * CTR_EL0 [3:0]   : Log2 of number of 4-byte words in smallest icache line
     */
    if (cache_info == 0)
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(cache_info));
    dcache_line_size = 4 << (cache_info >> 16 & 0xf);
    icache_line_size = 4 << (cache_info & 0xf);

    /* Flush data cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, dcache_line_size);
if ((unsigned long)addr > (unsigned long)beg_uint) { while(1); }
    do {
        __asm__ __volatile__("dc cvau, %0" : : "r"(addr) : "memory");
        addr += dcache_line_size;
    } while (addr != ALIGN_FORWARD(end_uint, dcache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Invalidate instruction cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, icache_line_size);
    do {
        __asm__ __volatile__("ic ivau, %0" : : "r"(addr) : "memory");
        addr += icache_line_size;
    } while (addr != ALIGN_FORWARD(end_uint, icache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Instruction Synchronization Barrier */
    __asm__ __volatile__("isb" : : : "memory");
}
#endif

NO_OPTIMIZE
static void
restart_fast_path()
{
  int mtcp_sys_errno;
  void *addr = mmap_fixed_noreplace(rinfo.restore_addr, rinfo.restore_size,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  size_t offset = (char *)&restorememoryareas - rinfo.text_addr;
  rinfo.restorememoryareas_fptr = (fnptr_t)(rinfo.restore_addr + offset);

  /* For __arm__ and __aarch64__ will need to invalidate cache after this.
   */
  mtcp_memcpy(rinfo.restore_addr, rinfo.text_addr, rinfo.text_size);
  mtcp_memcpy(rinfo.restore_addr + rinfo.text_size, &rinfo, sizeof(rinfo));
  void *stack_ptr = rinfo.restore_addr + rinfo.restore_size - MB;

  // The kernel call, __ARM_NR_cacheflush is avail. for __arm__, which
  //   requires kernel privilege to flush cache.  For __aarch64__  user-space suffices
  //   (and glibc clear_cache() is not available in most distros)
/*
  // Not avail. for __aarch64__, but should try this for __arm__:
  mtcp_sys_errno = 0;
  int rc1 = mtcp_sys_kernel_cacheflush(rinfo.restore_addr, rinfo.restore_addr + rinfo.text_size, 0);
  if (rc1 !=0) { MTCP_PRINTF("mtcp_sys_kernel_cacheflush failed; errno: %d\n", mtcp_sys_errno); }
  mtcp_sys_errno = 0;
  int rc2 = mtcp_sys_kernel_cacheflush(rinfo.restore_addr + rinfo.text_size,
                                       rinfo.restore_addr + rinfo.text_size + sizeof(rinfo), 0);
  if (rc2 !=0) { MTCP_PRINTF("mtcp_sys_kernel_cacheflush failed\n"); }
  mtcp_sys_mprotect(rinfo.restore_addr, rinfo.restore_size,
                             PROT_READ | PROT_EXEC);
*/

#if defined(__INTEL_COMPILER) && defined(__x86_64__)
  asm volatile ("mfence" ::: "memory"); // memfence() defined in dmtcpplugin.cpp
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
                CLEAN_FOR_64_BIT(xor %%ebp, %%ebp)
                  : : "g" (stack_ptr) : "memory");

  // This is copied from gcc assembly output for:
  // rinfo.restorememoryareas_fptr(&rinfo);
  // Intel icc-13.1.3 output uses register rbp here.  It's no longer available.
  asm volatile(
   // 112 = offsetof(RestoreInfo, rinfo.restorememoryareas_fptr)
   // NOTE: Update the offset when adding fields to the RestoreInfo struct
   "movq    112+rinfo(%%rip), %%rdx;" /* rinfo.restorememoryareas_fptr */
   "leaq    rinfo(%%rip), %%rdi;"    /* &rinfo */
   "movl    $0, %%eax;"
   "call    *%%rdx"
   : : );
  /* NOTREACHED */
#endif /* if defined(__INTEL_COMPILER) && defined(__x86_64__) */

#if defined(__arm__) || defined(__aarch64__)
# if defined(__aarch64__)
  // We would like to use the GCC builtin, __sync_synchronize()
  //  but it doesn't appear to be portable to arm/aarch64 as of Aug., 2018
  RMB; WMB; IMB;
  // The call to clear_icache() wasn't effective:
  //   clear_icache(rinfo.restore_addr, rinfo.restore_addr + rinfo.restore_size);
  // So, now we're using the loop to read memory into dummy, below, as a hack.
  // Apparently, this assembly instruction for "invalidate cache" requires
  //   kernel privilege:
  //   asm volatile (".arch armv8.1-a\n\t ic iallu\n\t" : : : "memory");
  // This logic is a hack to make sure that the cache recognizes new mmap region
  // Why can't gcc or glibc provide a working 'man 2 cacheflush' to correspond
  //   to the man page in Ubuntu 16.04?  (Or maybe clear_cache()?)
  char dummy;
  for (char *ptr = rinfo.restore_addr; ptr < rinfo.restore_addr + rinfo.restore_size; ptr++) {
    dummy = dummy ^ *ptr;
  }
  asm volatile("dsb ish" : : : "memory");
  RMB; WMB; IMB;
# else /* else if 0 */
  // FIXME:  Test if this delay loop is no longer needed for __arm__.
  //     We should be able to replace this by:
  //     mtcp_sys_kernel_cacheflush(rinfo.restore_addr,
  //                                rinfo.restore_addr + rinfo.text_size, 0);
  //     If that works, then delete this delay loop code.

  /* This delay loop was required for:
   *    ARM v7 (rev 3, v71), SAMSUNG EXYNOS5 (Flattened Device Tree)
   *    gcc-4.8.1 (Ubuntu pre-release for 14.04) ; Linux 3.13.0+ #54
   */
  MTCP_PRINTF("*** WARNING: %s:%d: Delay loop on restart for older ARM CPUs\n"
              "*** Consider removing this line for newer CPUs.\n",
              __FILE__, __LINE__);
  { int x = 10000000;
    int y = 1000000000;
    for (; x > 0; x--) {
      for (; y > 0; y--) {}
    }
  }
# endif /* if defined(__aarch64__) */
#endif /* if defined(__arm__) || defined(__aarch64__) */

  DPRINTF("We have copied mtcp_restart to higher address.  We will now\n"
          "    jump into a copy of restorememoryareas().\n");

  DPRINTF("rinfo address: %p current vdso: %p\n", &rinfo, rinfo.currentVdsoStart);

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
# ifndef __clang__

                /* This next assembly language confuses gdb.  Set a future
                   future breakpoint, or attach after this point, if in gdb.
                   It's here to force a hard error early, in case of a bug.*/
                CLEAN_FOR_64_BIT(xor %%ebp, %%ebp)
# else /* ifndef __clang__ */

                /* Even with -O0, clang-3.4 uses register ebp after this
                   statement. */
# endif /* ifndef __clang__ */
                : : "g" (stack_ptr) : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0\n\t"
                : : "r" (stack_ptr) : "memory");

  /* If we're going to have an error, force a hard error early, to debug. */
  asm volatile ("mov fp,#0\n\tmov ip,#0\n\tmov lr,#0" : :);
#elif defined(__aarch64__)
  asm volatile ("mov sp,%0\n\t"
                : : "r" (stack_ptr) : "memory");

  /* If we're going to have an error, force a hard error early, to debug. */

  // FIXME:  Add a hard error here in assembly.
#else /* if defined(__i386__) || defined(__x86_64__) */
# error "assembly instruction not translated"
#endif /* if defined(__i386__) || defined(__x86_64__) */

  /* IMPORTANT:  We just changed to a new stack.  The call frame for this
   * function on the old stack is no longer available.  The only way to pass
   * rinfo into the next function is by passing a pointer to a global variable
   * We call restorememoryareas_fptr(), which points to the copy of the
   * function in higher memory.  We will be unmapping the original fnc.
   */
  rinfo.restorememoryareas_fptr(&rinfo, &lh_info);

  /* NOTREACHED */
}

NO_OPTIMIZE
static void
restart_slow_path()
{
  restorememoryareas(&rinfo, &lh_info);
}

// Used by util/readdmtcp.sh
// So, we use mtcp_printf to stdout instead of MTCP_PRINTF (diagnosis for DMTCP)
static void
mtcp_simulateread(int fd, MtcpHeader *mtcpHdr)
{
  int mtcp_sys_errno;

  // Print miscellaneous information:
  char buf[MTCP_SIGNATURE_LEN + 1];

  mtcp_memcpy(buf, mtcpHdr->signature, MTCP_SIGNATURE_LEN);
  buf[MTCP_SIGNATURE_LEN] = '\0';
  mtcp_printf("\nMTCP: %s", buf);
  mtcp_printf("**** mtcp_restart (will be copied here): %p..%p\n",
              mtcpHdr->restore_addr,
              mtcpHdr->restore_addr + mtcpHdr->restore_size);
  mtcp_printf("**** DMTCP entry point (ThreadList::postRestart()): %p\n",
              mtcpHdr->post_restart);
  mtcp_printf("**** brk (sbrk(0)): %p\n", mtcpHdr->saved_brk);
  mtcp_printf("**** vdso: %p..%p\n", mtcpHdr->vdsoStart, mtcpHdr->vdsoEnd);
  mtcp_printf("**** vvar: %p..%p\n", mtcpHdr->vvarStart, mtcpHdr->vvarEnd);
  mtcp_printf("**** end of stack: %p\n", mtcpHdr->end_of_stack);

  Area area;
  mtcp_printf("\n**** Listing ckpt image area:\n");
  while (1) {
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) {
      break;
    }
    if ((area.properties & DMTCP_ZERO_PAGE) == 0 &&
        (area.properties & DMTCP_SKIP_WRITING_TEXT_SEGMENTS) == 0) {
      void *addr = mtcp_sys_mmap(0, area.size, PROT_WRITE | PROT_READ,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (addr == MAP_FAILED) {
        MTCP_PRINTF("***Error: mmap failed; errno: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
      mtcp_readfile(fd, addr, area.size);
      if (mtcp_sys_munmap(addr, area.size) == -1) {
        MTCP_PRINTF("***Error: munmap failed; errno: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
    }

    mtcp_printf("%p-%p %c%c%c%c "

                // "%x %u:%u %u"
                "          %s\n",
                area.addr, area.addr + area.size,
                (area.prot & PROT_READ  ? 'r' : '-'),
                (area.prot & PROT_WRITE ? 'w' : '-'),
                (area.prot & PROT_EXEC  ? 'x' : '-'),
                (area.flags & MAP_SHARED ? 's'
                 : (area.flags & MAP_ANONYMOUS ? 'p' : '-')),

                // area.offset, area.devmajor, area.devminor, area.inodenum,
                area.name);
  }
}

NO_OPTIMIZE
static void
restorememoryareas(RestoreInfo *rinfo_ptr, LowerHalfInfo_t *linfo_ptr)
{
  int mtcp_sys_errno;

  /* the address has been changed, the values become 0 */
  DPRINTF("rinfo address: %p local rinfo ptr: %p current vdso: global(%p) local(%p)\n",
	  &rinfo, rinfo_ptr, rinfo.currentVdsoStart, rinfo_ptr->currentVdsoStart);
  DPRINTF("Entering copy of restorememoryareas().  Will now unmap old memory"
          "\n    and restore memory sections from the checkpoint image.\n");

  DPRINTF("DPRINTF may fail when we unmap, since strings are in rodata.\n"
          "But we may be lucky if the strings have been cached by the O/S\n"
          "or if compiler uses relative addressing for rodata with -fPIC\n");

  if (rinfo_ptr->use_gdb) {
    MTCP_PRINTF("Called with --use-gdb.  A useful command is:\n"
                "    (gdb) lh_info proc mapping");
    if (rinfo_ptr->text_offset != -1) {
      MTCP_PRINTF("Called with --text-offset 0x%x.  A useful command is:\n"
                  "(gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
                  rinfo_ptr->text_offset,
                  rinfo_ptr->restore_addr + rinfo_ptr->text_offset);
#if defined(__i386__) || defined(__x86_64__)
      asm volatile ("int3"); // Do breakpoint; send SIGTRAP, caught by gdb
#else /* if defined(__i386__) || defined(__x86_64__) */
      MTCP_PRINTF(
        "IN GDB: interrupt (^C); add-symbol-file ...; (gdb) print x=0\n");
      { int x = 1; while (x) {}
      }                         // Stop execution for user to type command.
#endif /* if defined(__i386__) || defined(__x86_64__) */
    }
  }

  RestoreInfo restore_info;
  LowerHalfInfo_t lh_info;
  mtcp_memcpy(&restore_info, rinfo_ptr, sizeof(restore_info));
  mtcp_memcpy(&lh_info, linfo_ptr, sizeof(lh_info));
  if (rinfo_ptr->saved_brk != NULL) {
    // Now, we can do the pending mtcp_sys_brk(rinfo.saved_brk).
    // It's now safe to do this, even though it can munmap memory holding rinfo.
    if ((intptr_t)mtcp_sys_brk(rinfo_ptr->saved_brk) == -1) {
       MTCP_PRINTF("error restoring brk: %d\n", mtcp_sys_errno);
    }
  }

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax, %%eax; movw %%ax, %%fs)
                  : : : CLEAN_FOR_64_BIT(eax));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(0);  /* Uses 'mcr', a kernel-mode instr. on ARM */
#elif defined(__aarch64__)
# warning __FUNCTION__ "TODO: Implementation for ARM64"
#endif /* if defined(__i386__) || defined(__x86_64__) */

  /* Unmap everything except for vdso, vvar, vsyscall and this image as
   *  everything we need is contained in the libmtcp.so image.
   * Unfortunately, in later Linuxes, it's important also not to wipe
   *   out [vsyscall] if it exists (we may not have permission to remove it).
   * Further, if the [vdso] when we restart is different from the old
   *   [vdso] that was saved at checkpoint time, then we need to overwrite
   *   the old vdso with the new one (using mremap).
   *   Similarly for vvar.
   */
  unmap_memory_areas_and_restore_vdso(&restore_info, &lh_info);
  /* Restore memory areas */
  DPRINTF("restoring memory areas\n");
  readmemoryareas(restore_info.fd, restore_info.endOfStack);

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", restore_info.fd);
  mtcp_sys_close(restore_info.fd);
  double readTime = 0.0;
#ifdef TIMING
  struct timeval endValue;
  mtcp_sys_gettimeofday(&endValue, NULL);
  struct timeval diff;
  timersub(&endValue, &restore_info.startValue, &diff);
  readTime = diff.tv_sec + (diff.tv_usec / 1000000.0);
#endif

  IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

  /* Restore libc */
  DPRINTF("Memory is now restored.  Will next restore libc internals.\n");
  restore_libc(&restore_info.motherofall_tls_info, restore_info.tls_pid_offset,
               restore_info.tls_tid_offset, restore_info.myinfo_gs);

  /* System calls and libc library calls should now work. */

  DPRINTF("MTCP restore is now complete.  Continuing by jumping to\n"
          "  ThreadList::postRestart() back inside libdmtcp.so: %p...\n",
          restore_info.post_restart);

  if (restore_info.mtcp_restart_pause) {
    MTCP_PRINTF(
      "\nStopping due to env. var DMTCP_RESTART_PAUSE or MTCP_RESTART_PAUSE\n"
      "(DMTCP_RESTART_PAUSE can be set after creating the checkpoint image.)\n"
      "Attach to the computation with GDB from another window:\n"
      "(This won't work well unless you configure DMTCP with --enable-debug)\n"
      "  gdb PROGRAM_NAME %d\n"
      "You should now be in 'ThreadList::postRestartDebug()'\n"
      "  (gdb) list\n"
      "  (gdb) p dummy = 0\n"
      "  # Since Linux 3.10 (prctl:PR_SET_MM), you will also need to do:\n"
      "  (gdb) source DMTCP_ROOT/util/gdb-add-symbol-files-all\n"
      "  (gdb) add-symbol-files-all\n",
      mtcp_sys_getpid()
    );
    restore_info.post_restart_debug(readTime, restore_info.mtcp_restart_pause);
    // volatile int dummy = 1;
    // while (dummy);
  } else {
    restore_info.post_restart(readTime);
  }
  // NOTREACHED
}

static int
skip_lh_memory_region_ckpting(const Area *area, LowerHalfInfo_t *lh_info)
{
  static MmapInfo_t *g_list = NULL;
  static int g_numMmaps = 0;
  int i = 0;

  getMmappedList_t fnc = (getMmappedList_t)lh_info->getMmappedListFptr;
  if (fnc) {
    g_list = fnc(&g_numMmaps);
  }
  if (area->addr == lh_info->startText ||
      mtcp_strstr(area->name, "/dev/zero") ||
      mtcp_strstr(area->name, "/dev/kgni") ||
      mtcp_strstr(area->name, "/dev/xpmem") ||
      mtcp_strstr(area->name, "/dev/shm") ||
      mtcp_strstr(area->name, "/SYS") ||
      area->addr == lh_info->startData) {
    return 1;
  }
  if (!g_list) return 0;
  for (i = 0; i < g_numMmaps; i++) {
    void *lhMmapStart = g_list[i].addr;
    void *lhMmapEnd = (VA)g_list[i].addr + g_list[i].len;
    if (!g_list[i].unmapped &&
        regionContains(lhMmapStart, lhMmapEnd, area->addr, area->endAddr)) {
      return 1;
    } else if (!g_list[i].unmapped &&
               regionContains(area->addr, area->endAddr,
                              lhMmapStart, lhMmapEnd)) {
    }
  }
  return 0;
}

NO_OPTIMIZE
static void
unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo, LowerHalfInfo_t *lh_info)
{
  /* Unmap everything except this image, vdso, vvar and vsyscall. */
  int mtcp_sys_errno;
  Area area;
  /* If we haven't set currentVdsoStart, it should be NULL, and we should get it by parsing vdso memory segment.
   * If we have mremap it, 1) we lost vdso label, we just use what we saved. 2) vdso label is still there
   * should be the same with what we saved */
  VA vdsoStart = rinfo->currentVdsoStart;
  VA vdsoEnd = rinfo->currentVdsoEnd;;
  VA vvarStart = NULL;
  VA vvarEnd = NULL;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (area.addr >= rinfo->restore_addr && area.addr < rinfo->restore_end) {
      // Do not unmap this restore image.
    } else if (mtcp_strcmp(area.name, "[vdso]") == 0) {
      if (rinfo->currentVdsoStart != NULL && rinfo->currentVdsoStart != area.addr) {
        MTCP_PRINTF("***WARNING: current vdso %p is not matched with what rinfo saved %p\n",
                    area.addr, rinfo->currentVdsoStart);
      }
      // Do not unmap vdso.
      vdsoStart = area.addr;
      vdsoEnd = area.endAddr;
      DPRINTF("***INFO: vDSO found (%p..%p)\n original vDSO: (%p..%p)\n",
              area.addr, area.endAddr, rinfo->vdsoStart, rinfo->vdsoEnd);
    }
#if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
    else if (area.addr == 0xfffe0000 && area.size == 4096) {
      // It's a vdso page from a time before Linux displayed the annotation.
      // Do not unmap vdso.
    }
#endif /* if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
          */
    else if (mtcp_strcmp(area.name, "[vvar]") == 0) {
      if (rinfo->currentVvarStart != NULL && rinfo->currentVvarStart != area.addr) {
        MTCP_PRINTF("***WARNING: current vvar %p is not matched with what rinfo saved %p\n",
                    area.addr, rinfo->currentVvarStart);
      }
      // Do not unmap vvar.
      vvarStart = area.addr;
      vvarEnd = area.endAddr;
    } else if (mtcp_strcmp(area.name, "[vsyscall]") == 0) {
      // Do not unmap vsyscall.
    } else if (mtcp_strcmp(area.name, "[vectors]") == 0) {
      // Do not unmap vectors.  (used in Linux 3.10 on __arm__)
    } else if (mtcp_strstr(area.name, "/dev/shm/mpich")) {
      // Do not unmap shm region created by MPI
      // FIXME: It's possible that this region conflicts with some region
      //        in the checkpoint image.
    } else if (skip_lh_memory_region_ckpting(&area, lh_info)) {
      // Do not unmap lower half
      DPRINTF("Skipping lower half memory section: %p-%p\n",
              area.addr, area.endAddr);
    } else if (area.addr == rinfo->currentVdsoStart) {
      /* We need it becasue the memory segment might lost [vdso] label after mremap */
      DPRINTF("Skipping temporary vDSO section: %p-%p\n",
              area.addr, area.endAddr);
    } else if (area.addr == rinfo->currentVvarStart) {
      DPRINTF("Skipping temporary vvar section: %p-%p\n",
              area.addr, area.endAddr);
    } else if (area.size > 0) {
      DPRINTF("***INFO: munmapping (%p..%p)\n", area.addr, area.endAddr);
      if (mtcp_sys_munmap(area.addr, area.size) == -1) {
        MTCP_PRINTF("***WARNING: %s(%x): munmap(%p, %d) failed; errno: %d\n",
                    area.name, area.flags, area.addr, area.size,
                    mtcp_sys_errno);
        mtcp_abort();
      }

      // Rewind and reread maps.
      mtcp_sys_lseek(mapsfd, 0, SEEK_SET);
    }
  }
  mtcp_sys_close(mapsfd);

  // If the vvar (or vdso) segment does not exist, then rinfo->vvarStart and
  // rinfo->vvarEnd are both 0 (or the vdso equivalents). We check for those to
  // ensure we don't get a false positive here.
  if ((vdsoStart == vvarEnd && rinfo->vdsoStart != rinfo->vvarEnd &&
       rinfo->vvarStart != 0 && rinfo->vvarEnd != 0) ||
      (vvarStart == vdsoEnd && rinfo->vvarStart != rinfo->vdsoEnd &&
       rinfo->vdsoStart != 0 && rinfo->vdsoEnd != 0)) {
    MTCP_PRINTF("***Error: vdso/vvar order was different during ckpt.\n");
    mtcp_abort();
  }

  if (vdsoEnd - vdsoStart != rinfo->vdsoEnd - rinfo->vdsoStart) {
    MTCP_PRINTF("***Error: vdso size mismatch.\n");
    mtcp_abort();
  }

  if (vvarEnd - vvarStart != rinfo->vvarEnd - rinfo->vvarStart) {
    MTCP_PRINTF("***Error: vvar size mismatch.\n");
    mtcp_abort();
  }

  if (vdsoStart == rinfo->vdsoStart) {
    // If the new vDSO is at the same address as the old one, do nothing.
    MTCP_ASSERT(vvarStart == rinfo->vvarStart);
    return;
  }

  // Check for overlap between newer and older vDSO/vvar sections.
  if (
#if 0
      doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vdsoStart, rinfo->vdsoEnd - rinfo->vdsoStart) ||
      doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vvarStart, rinfo->vvarEnd - rinfo->vvarStart) ||
      doAreasOverlap(vvarStart, vvarEnd - vvarStart,
                     rinfo->vdsoStart, rinfo->vdsoEnd - rinfo->vdsoStart) ||
      doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vvarStart, rinfo->vvarEnd - rinfo->vvarStart)
#else
      // We will move vvar first, if original vvar doesn't overlap with
      // current vdso.  After that, it should be possible to move vdso to its
      // original position.
      doAreasOverlap(rinfo->vvarStart, vvarEnd - vvarStart,
                     vdsoStart, vdsoEnd - vdsoStart)
#endif
     ) // FIXME:  We can temporarily move vvar or vdso to a third,
       //         non-conflicting location, if a future kernel causes
       //         this error condition to happen.
  { MTCP_PRINTF("*** MTCP Error: Overlapping addresses for older and newer\n"
                "                vDSO/vvar sections.\n"
                "      vdsoStart: %p vdsoEnd: %p vvarStart: %p vvarEnd: %p\n"
                "rinfo:vdsoStart: %p vdsoEnd: %p vvarStart: %p vvarEnd: %p\n"
                "(SEE FIXME comment in source code for how to fix this.)\n",
                vdsoStart,
                vdsoEnd,
                vvarStart,
                vvarEnd,
                rinfo->vdsoStart,
                rinfo->vdsoEnd,
                rinfo->vvarStart,
                rinfo->vvarEnd);
    mtcp_abort();
  }

  // Move vvar to original location, followed by same for vdso
  if (vvarStart != NULL) {
    int rc = mremap_move(rinfo->vvarStart, vvarStart, vvarEnd - vvarStart);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to remap vvarStart to old value "
                  "%p -> %p); errno: %d.\n",
                  vvarStart, rinfo->vvarStart, mtcp_sys_errno);
      mtcp_abort();
    }
#if defined(__i386__)
    // FIXME: For clarity of code, move this i386-specific code to a function.
    vvar = mmap_fixed_noreplace(vvarStart, vvarEnd - vvarStart,
                                PROT_EXEC | PROT_WRITE | PROT_READ,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (vvar == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vvar; errno: %d\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vvar == vvarStart);
    // On i386, only the first page is readable. Reading beyond that
    // results in a bus error.
    // Arguably, this is a bug in the kernel, since /proc/*/maps indicates
    // that both pages of vvar memory have read permission.
    // This issue arose due to a change in the Linux kernel pproximately in
    // version 4.0
    // TODO: Find a way to automatically detect the readable bytes.
    mtcp_memcpy(vvarStart, rinfo->vvarStart, MTCP_PAGE_SIZE);
#endif /* if defined(__i386__) */
  }

  if (vdsoStart != NULL) {
    int rc = mremap_move(rinfo->vdsoStart, vdsoStart, vdsoEnd - vdsoStart);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to remap vdsoStart to old value "
                  "%p -> %p); errno: %d.\n",
                  vdsoStart, rinfo->vdsoStart, mtcp_sys_errno);
      mtcp_abort();
    }
#if defined(__i386__)
    // FIXME: For clarity of code, move this i386-specific code to a function.
    // In commit dec2c26c1eb13eb1c12edfdc9e8e811e4cc0e3c2 , the mremap
    // code above was added, and then caused a segfault on restart for
    // __i386__ in CentOS 7.  In that case ENABLE_VDSO_CHECK was not defined.
    // This version was needed in that commit for __i386__
    // (i.e., for multi-arch.sh) to succeed.
    // Newer Linux kernels, such as __x86_64__, provide a separate vsyscall
    // segment
    // for kernel calls while using vdso for system calls that can be
    // executed purely in user space through kernel-specific user-space code.
    // On older kernels like __x86__, both purposes are squeezed into vdso.
    // Since vdso will use randomized addresses (unlike the standard practice
    // for vsyscall), this implies that kernel calls on __x86__ can go through
    // randomized addresses, and so they need special treatment.
    vdso = mmap_fixed_noreplace(vdsoStart, vdsoEnd - vdsoStart,
                         PROT_EXEC | PROT_WRITE | PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    // The new vdso was remapped to the location of the old vdso, since the
    // restarted application code remembers the old vdso address.
    // But for __i386__, a kernel call will go through the old vdso address
    // into the kernel, and then the kernel will return to the new vdso address
    // that was created by this kernel.  So, we need to copy the new vdso
    // code from its current location at the old vdso address back into
    // the new vdso address that was just mmap'ed.
    if (vdso == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vdso; errno: %d\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vdso == vdsoStart);
    mtcp_memcpy(vdsoStart, rinfo->vdsoStart, vdsoEnd - vdsoStart);
#endif /* if defined(__i386__) */
  } else {
    MTCP_PRINTF("***WARNING: vdso is missing\n");
  }
}

/**************************************************************************
 *
 *  Read memory area descriptors from checkpoint file
 *  Read memory area contents and/or mmap original file
 *  Four cases: MAP_ANONYMOUS (if file /proc/.../maps reports file,
 *		   handle it as if MAP_PRIVATE and not MAP_ANONYMOUS,
 *		   but restore from ckpt image: no copy-on-write);
 *		 private, currently assumes backing file exists
 *               shared, but need to recreate file;
 *		 shared and file currently exists
 *		   (if writeable by us and memory map has write
 *		    protection, then write to it from checkpoint file;
 *		    else skip ckpt image and map current data of file)
 *		 NOTE:  Linux option MAP_SHARED|MAP_ANONYMOUS
 *		    currently not supported; result is undefined.
 *		    If there is an important use case, we will fix this.
 *		 (NOTE:  mmap requires that if MAP_ANONYMOUS
 *		   was not set, then mmap must specify a backing store.
 *		   Further, a reference by mmap constitutes a reference
 *		   to the file, and so the file cannot truly be deleted
 *		   until the process no longer maps it.  So, if we don't
 *		   see the file on restart and there is no MAP_ANONYMOUS,
 *		   then we have a responsibility to recreate the file.
 *		   MAP_ANONYMOUS is not currently POSIX.)
 *
 **************************************************************************/
static void
readmemoryareas(int fd, VA endOfStack)
{
  while (1) {
    if (read_one_memory_area(fd, endOfStack) == -1) {
      break; /* error */
    }
  }
#if defined(__arm__) || defined(__aarch64__)

  /* On ARM, with gzip enabled, we sometimes see SEGFAULT without this.
   * The SEGFAULT occurs within the initial thread, before any user threads
   * are unblocked.  WHY DOES THIS HAPPEN?
   */
  WMB;
#endif /* if defined(__arm__) || defined(__aarch64__) */
}

NO_OPTIMIZE
static int
read_one_memory_area(int fd, VA endOfStack)
{
  int mtcp_sys_errno;
  int imagefd;
  void *mmappedat;
  int try_skipping_existing_segment = 0;

  /* Read header of memory area into area; mtcp_readfile() will read header */
  Area area;

  mtcp_readfile(fd, &area, sizeof area);
  if (area.size == -1) {
    return -1;
  }

  if (area.name[0] && mtcp_strstr(area.name, "[heap]")
      && mtcp_sys_brk(NULL) != area.addr + area.size) {
    DPRINTF("WARNING: break (%p) not equal to end of heap (%p)\n",
            mtcp_sys_brk(NULL), area.addr + area.size);
  }
  /* MAP_GROWSDOWN flag is required for stack region on restart to make
   * stack grow automatically when application touches any address within the
   * guard page region(usually, one page less then stack's start address).
   *
   * The end of stack is detected dynamically at checkpoint time. See
   * prepareMtcpHeader() in threadlist.cpp and ProcessInfo::growStack()
   * in processinfo.cpp.
   */
  if (area.name[0] && (mtcp_strstr(area.name, "[stack]") ||
                       (area.endAddr == endOfStack))) {
    area.flags = area.flags | MAP_GROWSDOWN;
    DPRINTF("Detected stack area. End of stack (%p); Area end address (%p)\n",
            endOfStack, area.endAddr);
  }

  // We could have replaced MAP_SHARED with MAP_PRIVATE in writeckpt.cpp
  // instead of here. But we do it this way for debugging purposes. This way,
  // readdmtcp.sh will still be able to properly list the shared memory areas.
  if (area.flags & MAP_SHARED) {
    area.flags = area.flags ^ MAP_SHARED;
    area.flags = area.flags | MAP_PRIVATE | MAP_ANONYMOUS;
  }

  /* Now mmap the data of the area into memory. */

  /* CASE MAPPED AS ZERO PAGE: */
  if ((area.properties & DMTCP_ZERO_PAGE) != 0) {
    DPRINTF("restoring non-rwx anonymous area, %p bytes at %p\n",
            area.size, area.addr);
    mmappedat = mmap_fixed_noreplace(area.addr, area.size,
                              area.prot,
                              area.flags | MAP_FIXED, -1, 0);

    if (mmappedat != area.addr) {
      MTCP_PRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, area.size, area.addr);
      mtcp_abort();
    }
  }

#ifdef FAST_RST_VIA_MMAP
    /* CASE MAP_ANONYMOUS with FAST_RST enabled
     * We only want to do this in the MAP_ANONYMOUS case, since we don't want
     *   any writes to RAM to be reflected back into the underlying file.
     * Note that in order to map from a file (ckpt image), we must turn off
     *   anonymous (~MAP_ANONYMOUS).  It's okay, since the fd
     *   should have been opened with read permission, only.
     */
    else if (area.flags & MAP_ANONYMOUS) {
      mmapfile (fd, area.addr, area.size, area.prot,
                area.flags & ~MAP_ANONYMOUS);
    }
#endif

  /* CASE MAP_ANONYMOUS (usually implies MAP_PRIVATE):
   * For anonymous areas, the checkpoint file contains the memory contents
   * directly.  So mmap an anonymous area and read the file into it.
   * If file exists, turn off MAP_ANONYMOUS: standard private map
   */
  else if (area.flags & MAP_ANONYMOUS) {
    /* If there is a filename there, though, pretend like we're mapping
     * to it so a new /proc/self/maps will show a filename there like with
     * original process.  We only need read-only access because we don't
     * want to ever write the file.
     */

    imagefd = -1;
    if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
      imagefd = mtcp_sys_open(area.name, O_RDONLY, 0);
      if (imagefd >= 0) {
        /* If the current file size is smaller than the original, we map the region
         * as private anonymous. Note that with this we lose the name of the region
         * but most applications may not care.
         */
        off_t curr_size = mtcp_sys_lseek(imagefd, 0, SEEK_END);
        MTCP_ASSERT(curr_size != -1);
        if (curr_size < area.offset + area.size) {
          mtcp_sys_close(imagefd);
          imagefd = -1;
          area.offset = 0;
        } else {
          area.flags ^= MAP_ANONYMOUS;
        }
      }
    }

    if (area.flags & MAP_ANONYMOUS) {
      DPRINTF("restoring anonymous area, %p  bytes at %p\n",
              area.size, area.addr);
    } else {
      DPRINTF("restoring to non-anonymous area from anonymous area,"
              " %p bytes at %p from %s + 0x%X\n",
              area.size, area.addr, area.name, area.offset);
    }

    /* Create the memory area */

    /* POSIX says mmap would unmap old memory.  Munmap never fails if args
     * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
     * mtcp_safemmap here to check for address conflicts.
     */
#ifdef HUGEPAGES
    if (area.hugepages) {
      area.flags |= MAP_HUGETLB;
    }
#endif
    mmappedat = mmap_fixed_noreplace(area.addr, area.size,
	                             area.prot | PROT_WRITE,
	                             area.flags, imagefd, area.offset);

    if (mmappedat == MAP_FAILED) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, area.size, area.addr);
      if (mtcp_sys_errno == ENOMEM) {
        MTCP_PRINTF(
          "\n**********************************************************\n"
          "****** Received ENOMEM.  Trying to continue, but may fail.\n"
          "****** Please run 'free' to see if you have enough swap space.\n"
          "**********************************************************\n\n");
      }
      try_skipping_existing_segment = 1;
    }
    if (mmappedat != area.addr && !try_skipping_existing_segment) {
      MTCP_PRINTF("area at %p got mmapped to %p\n", area.addr, mmappedat);
      mtcp_abort();
    }

#if 0

    /*
     * The function is not used but is truer to maintaining the user's
     * view of /proc/ * /maps. It can be enabled again in the future after
     * we fix the logic to handle zero-sized files.
     */
    if (imagefd >= 0) {
      adjust_for_smaller_file_size(&area, imagefd);
    }
#endif /* if 0 */

    /* Close image file (fd only gets in the way) */
    if (imagefd >= 0 && !(area.flags & MAP_ANONYMOUS)) {
      mtcp_sys_close(imagefd);
    }

    if (try_skipping_existing_segment) {
      // This fails on teracluster.  Presumably extra symbols cause overflow.
      mtcp_skipfile(fd, area.size);
    } else if ((area.properties & DMTCP_SKIP_WRITING_TEXT_SEGMENTS) == 0) {
      /* This mmapfile after prev. mmap is okay; use same args again.
       *  Posix says prev. map will be munmapped.
       */

      /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
      mtcp_readfile(fd, area.addr, area.size);
      if (!(area.prot & PROT_WRITE)) {
        if (mtcp_sys_mprotect(area.addr, area.size, area.prot) < 0) {
          MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                      mtcp_sys_errno, area.size, area.addr);
          mtcp_abort();
        }
      }
    }
  }
  /* CASE NOT MAP_ANONYMOUS:
   * Otherwise, we mmap the original file contents to the area.
   * This case is now delegated to DMTCP.  Nothing to do for MTCP.
   */
  else { /* Internal error. */
    MTCP_ASSERT(0);
  }
  return 0;
}

#if 0

// See note above.
NO_OPTIMIZE
static void
adjust_for_smaller_file_size(Area *area, int fd)
{
  int mtcp_sys_errno;
  off_t curr_size = mtcp_sys_lseek(fd, 0, SEEK_END);

  if (curr_size == -1) {
    return;
  }
  if (area->offset + area->size > curr_size) {
    size_t diff_in_size = (area->offset + area->size) - curr_size;
    size_t anon_area_size = (diff_in_size + MTCP_PAGE_SIZE - 1)
      & MTCP_PAGE_MASK;
    VA anon_start_addr = area->addr + (area->size - anon_area_size);

    DPRINTF("For %s, current size (%ld) smaller than original (%ld).\n"
            "mmap()'ng the difference as anonymous.\n",
            area->name, curr_size, area->size);
    VA mmappedat = mmap_fixed_noreplace(anon_start_addr, anon_area_size,
                                 area->prot | PROT_WRITE,
                                 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                                 -1, 0);

    if (mmappedat == MAP_FAILED) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, anon_area_size, anon_start_addr);
    }
    if (mmappedat != anon_start_addr) {
      MTCP_PRINTF("area at %p got mmapped to %p\n", anon_start_addr, mmappedat);
      mtcp_abort();
    }
  }
}
#endif /* if 0 */


/*****************************************************************************
 *
 *  Restore the GDT entries that are part of a thread's state
 *
 *  The kernel provides set_thread_area system call for a thread to alter a
 *  particular range of GDT entries, and it switches those entries on a
 *  per-thread basis.  So from our perspective, this is per-thread state that is
 *  saved outside user addressable memory that must be manually saved.
 *
 *****************************************************************************/
void
restore_libc(ThreadTLSInfo *tlsInfo,
             int tls_pid_offset,
             int tls_tid_offset,
             MYINFO_GS_T myinfo_gs)
{
  int mtcp_sys_errno;

  /* Every architecture needs a register to point to the current
   * TLS (thread-local storage).  This is where we set it up.
   */

  /* Patch 'struct user_desc' (gdtentrytls) of glibc to contain the
   * the new pid and tid.
   */
  *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
             + tls_pid_offset) = mtcp_sys_getpid();
  if (mtcp_sys_kernel_gettid() == mtcp_sys_getpid()) {
    *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
               + tls_tid_offset) = mtcp_sys_getpid();
  }

  /* Now pass this to the kernel, so it can adjust the segment descriptor.
   * This will make different kernel calls according to the CPU architecture. */
  if (tls_set_thread_area(&(tlsInfo->gdtentrytls[0]), myinfo_gs) != 0) {
    MTCP_PRINTF("Error restoring GDT TLS entry; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  /* Finally, if this is i386, we need to set %gs to refer to the segment
   * descriptor that we're using above.  We restore the original pointer.
   * For the other architectures (not i386), the kernel call above
   * already did the equivalent work of setting up thread registers.
   */
#ifdef __i386__
  asm volatile ("movw %0,%%fs" : : "m" (tlsInfo->fs));
  asm volatile ("movw %0,%%gs" : : "m" (tlsInfo->gs));
#elif __x86_64__

  /* Don't directly set fs.  It would only set 32 bits, and we just
   *  set the full 64-bit base of fs, using sys_set_thread_area,
   *  which called arch_prctl.
   *asm volatile ("movl %0,%%fs" : : "m" (tlsInfo->fs));
   *asm volatile ("movl %0,%%gs" : : "m" (tlsInfo->gs));
   */
#elif defined(__arm__) || defined(__aarch64__)

  /* ARM treats this same as x86_64 above. */
#endif /* ifdef __i386__ */
}

NO_OPTIMIZE
static int
doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2)
{
  VA end1 = (char *)addr1 + size1;
  VA end2 = (char *)addr2 + size2;

  return (size1 > 0 && addr1 >= addr2 && addr1 < end2) ||
         (size2 > 0 && addr2 >= addr1 && addr2 < end1);
}

NO_OPTIMIZE
static int
hasOverlappingMapping(VA addr, size_t size)
{
  int mtcp_sys_errno;
  int ret = 0;
  Area area;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (doAreasOverlap(addr, size, area.addr, area.size)) {
      ret = 1;
      break;
    }
  }
  mtcp_sys_close(mapsfd);
  return ret;
}

// XXX: Define this on systems with older LD
#define OLDER_LD

NO_OPTIMIZE
static int
mremap_move(void *dest, void *src, size_t size) {
  int mtcp_sys_errno;
  if (dest == src) {
    return 0; // Success
  }
  void *rc = mtcp_sys_mremap(src, size, size, MREMAP_FIXED | MREMAP_MAYMOVE,
                             dest);
  if (rc == dest) {
    return 0; // Success
  } else if (rc == MAP_FAILED) {
    MTCP_PRINTF("***Error: failed to mremap; errno: %d.\n",
                mtcp_sys_errno);
    return -1; // Error
  } else {
    // We failed to move the memory to 'dest'.  Undo it.
    mremap_move(src, rc, size);
    return -1; // Error
  }
}

NO_OPTIMIZE
static void
getTextAddr(VA *text_addr, size_t *size)
{
  int mtcp_sys_errno;
  Area area;
  VA this_fn = (VA)&getTextAddr;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
#ifndef OLDER_LD
  size_t sz = 0;
#endif // ifndef OLDER_LD

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if ((mtcp_strendswith(area.name, BINARY_NAME) ||
         mtcp_strendswith(area.name, BINARY_NAME_M32)) &&
#ifdef OLDER_LD
        (area.prot & PROT_EXEC) &&
#else
        (area.prot & PROT_EXEC || area.prot & PROT_READ)) {
#endif

        /* On ARM/Ubuntu 14.04, mtcp_restart is mapped twice with RWX
         * permissions. Not sure why? Here is an example:
         *
         * 00008000-00010000 r-xp 00000000 b3:02 144874     .../bin/mtcp_restart
         * 00017000-00019000 rwxp 00007000 b3:02 144874     .../bin/mtcp_restart
         * befdf000-bf000000 rwxp 00000000 00:00 0          [stack]
         * ffff0000-ffff1000 r-xp 00000000 00:00 0          [vectors]
         */
#ifdef OLDER_LD
        (area.addr < this_fn && (area.addr + area.size) > this_fn)) {
      *text_addr = area.addr;
      *size = area.size;
      break;
#else
      if (area.addr < this_fn && !(area.prot & PROT_EXEC)) {
        *text_addr = area.addr;
        sz += area.size;
      } else if (area.addr < this_fn && (area.addr + area.size) > this_fn) {
        sz += area.size;
      } else if (area.addr > this_fn && area.prot & PROT_READ) {
        sz += area.size;
        break;
      }
#endif // ifdef OLDER_LD
    }
  }
#ifndef OLDER_LD
  *size = sz;
#endif // ifndef OLDER_LD
  mtcp_sys_close(mapsfd);
}

// gcc can generate calls to these.
// Eventually, we'll isolate the PIC code in a library, and this can go away.
void
__stack_chk_fail(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_fail_local(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_guard(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
_Unwind_Resume(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__gcc_personality_v0(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_check_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags)
{
  int mtcp_sys_errno;
  void *addr;
  int rc;

  /* Use mmap for this portion of checkpoint image. */
  addr = mmap_fixed_noreplace(buf, size, prot, flags,
                       fd, mtcp_sys_lseek(fd, 0, SEEK_CUR));
  if (addr != buf) {
    if (addr == MAP_FAILED) {
      MTCP_PRINTF("error %d reading checkpoint file\n", mtcp_sys_errno);
    } else {
      MTCP_PRINTF("Requested address %p, but got address %p\n", buf, addr);
    }
    mtcp_abort();
  }
  /* Now update fd so as to work the same way as readfile() */
  rc = mtcp_sys_lseek(fd, size, SEEK_CUR);
  if (rc == -1) {
    MTCP_PRINTF("mtcp_sys_lseek failed with errno %d\n", mtcp_sys_errno);
    mtcp_abort();
  }
}
#endif
