#include <stdbool.h>
#include <asm/prctl.h>
#define _GNU_SOURCE // needed for MREMAP_MAYMOVE
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/auxv.h>
#include <linux/limits.h>
#include "mtcp_restart.h"
#include "mtcp_sys.h"
#include "mtcp_util.h"
#include "../mpi-proxy-split/mana_header.h"
#include "mtcp_restart_plugin.h"
#include "mtcp_split_process.h"
#ifdef SINGLE_CART_REORDER
#include "../mpi-proxy-split/cartesian.h"
#include "../dmtcp/src/mtcp/mtcp_sys.h"
#endif

#include "config.h" // from dmtcp/include; used to compile mtcp_restart

// FIXME:  Remove PluginInfo from mtcp_restart_plugin.h in this directory.
//         Remove all lines with PluginInfo from dmtcp/src/mtcp/mtcp_restart.h
//         Create a new PR/commit for DMTCP based on the last.
//           In mtcp_restart.h:struct RestoreInfo, change the pluginInfo field
//           to be of type char[512]. The restart_plugin directory can
//           cast it to a local 'struct PluginInfo' that starts with
//           a size field, and then an lh_info_addr field, and then the
//           upper-half fields minLibsStart, maxLibsEnd, minHighMemStart, etc.
//           See the FIXME comment in mtcp_restart_plugin.h.
//         Push the PR into DMTCP master.
//         In the DMTCP submodule, git pull --rebase origin master
//         git submodule update
//         Remove this FIXME comment.
//         Create a new MANA PR from this, and the 'struct PluginInfo' field..

#define HAS_MAP_FIXED_NOREPLACE LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

// Using both methods to skip unmapping lower-half mmap'ed regions
// In theory, just one of these techniques should suffice.
#define USE_LH_MMAPS_ARRAY
#define USE_LOWER_BOUNDARY

# define GB (uint64_t)(1024 * 1024 * 1024)
#define ROUNDADDRUP(addr, size) ((addr + size - 1) & ~(size - 1))

NO_OPTIMIZE
char*
getCkptImageByRank(int rank, char **environ)
{
  if (rank < 0) {
    return NULL;
  }

  char *fname = NULL;
  char envKey[64] = {0};
  char rankStr[20] = {0};
  mtcp_itoa(rankStr, rank);
  mtcp_strcpy(envKey, "MANA_CkptImage_Rank_");
  mtcp_strncat(envKey, rankStr, mtcp_strlen(rankStr));
  return mtcp_getenv(envKey, environ);
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

// FIXME:  Replace '#if' in DMTCP definition of 'mmap_fixed_noreplace' w/ '#if'
//         and continue to use mmap_fixed_noreplace(..., ...| MAP_FIXED, ...)
//         and DMTCP definition can replace MAP_FIXED by MAP_FIXED_NOREPLACE.
#if HAS_MAP_FIXED_NOREPLACE
  int fd = mtcp_sys_open2("/dev/zero", O_RDONLY);
  void *addr = mtcp_sys_mmap(start1, len1, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                             fd, 0);
  MTCP_ASSERT(addr == start1);
  if (len2 > 0) {
    addr = mtcp_sys_mmap(start2, len2, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                         fd, 0);
    MTCP_ASSERT(addr == start2);
  }
#else
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
#endif
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

	// return result
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
int getCkptImageByDir(RestoreInfo *rinfo, char *buffer, size_t buflen, int rank) {
  if(!rinfo->pluginInfo.restartDir) {
    MTCP_PRINTF("***ERROR No restart directory found - cannot find checkpoint image by directory!");
    return -1;
  }

  size_t len = mtcp_strlen(rinfo->pluginInfo.restartDir);
  if(len >= buflen){
    MTCP_PRINTF("***ERROR Restart directory would overflow given buffer!");
    return -1;
  }
  mtcp_strcpy(buffer, rinfo->pluginInfo.restartDir); // start with directory

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

// This function searches for a free memory region to temporarily remap the vdso
// and vvar regions to, so that mtcp's vdso and vvar do not overlap with the
// checkpointed process' vdso and vvar.
static void
remap_vdso_and_vvar_regions(RestoreInfo *rinfo) {
  void *rc = NULL;
  uint64_t vvarStart = (uint64_t) rinfo->currentVvarStart;
  uint64_t vvarSize = rinfo->currentVvarEnd - rinfo->currentVvarStart;
  uint64_t vdsoStart = (uint64_t) rinfo->currentVdsoStart;
  uint64_t vdsoSize = rinfo->currentVdsoEnd - rinfo->currentVdsoStart;

  // Find an empty region in which to temporarily move the current vvar/vdso
  // CentOS-7 (kernel 3.10) has "vdso", but no "vvar".  So, test for vvar.
  Area new_vvar_area;
  // vvarExists and 'rinfo->currentVvarStart != NULL' are same.  Just verifying.
  int vvarExists = getMappedArea(&new_vvar_area, "[vvar]") == 1;
  if (vvarExists) {
    MTCP_ASSERT(rinfo->currentVvarStart == new_vvar_area.addr);
  } else {
    MTCP_ASSERT(rinfo->currentVvarStart == NULL && vvarSize == 0);
  }

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }
  // Look for unmapped region far below the future upper-half region, where
  // we can temporarily move the current vvar/vdso to.
  uint64_t vvarStartTmp = 0;
  uint64_t vdsoStartTmp = 0;
  Area area;
  uint64_t prev_end_addr = 0x10000;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (prev_end_addr + vvarSize + vdsoSize <= (uint64_t)area.addr) {
      vvarStartTmp = prev_end_addr;
      vdsoStartTmp = prev_end_addr + vvarSize;
      break;
    } else {
      prev_end_addr = ROUNDADDRUP((uint64_t)area.endAddr, MTCP_PAGE_SIZE);
    }
  }
  mtcp_sys_close(mapsfd);

  // We have now found a vvarStartTmp/vdsoStartTmp where we can move our
  // vvar/vdso to.  Move current vvar/vdso to a low address far from where
  // the upper half will be restore to, starting at address vvarStartTmp.
  // We will set the rinfo to pretend to dmtcp/src/mtcp/mtcp_restart.c
  // that vvarStartTmp is the current vvar/vdso.  After restoring the
  //  upper half, MTCP will then move it back to the pre-ckpt area
  if (vvarStart > 0 && vvarSize > 0) {
    if (vvarStartTmp == 0) {
      MTCP_PRINTF("No free region found to temporarily map vvar to\n");
      mtcp_abort();
    }
    rc = mtcp_sys_mremap(vvarStart, vvarSize, vvarSize,
                         MREMAP_FIXED | MREMAP_MAYMOVE, vvarStartTmp);
    if (rc == MAP_FAILED) {
      MTCP_PRINTF("mtcp_restart failed: "
                  "gdb --mpi \"DMTCP_ROOT/bin/mtcp_restart\" to debug.\n");
      mtcp_abort();
    }
  }

  if (vdsoStart > 0) {
    if (vdsoStartTmp == 0) {
      MTCP_PRINTF("No free region found to temporarily map vdso to\n");
      mtcp_abort();
    }
    rc = mtcp_sys_mremap(vdsoStart, vdsoSize, vdsoSize,
                         MREMAP_FIXED | MREMAP_MAYMOVE, vdsoStartTmp);
    if (rc == MAP_FAILED) {
      MTCP_PRINTF("mtcp_restart failed: "
                  "gdb --mpi \"DMTCP_ROOT/bin/mtcp_restart\" to debug.\n");
      mtcp_abort();
    }
  }

  rinfo->currentVvarStart = (VA) vvarStartTmp;
  rinfo->currentVvarEnd = (VA) vvarStartTmp + vvarSize;
  rinfo->currentVdsoStart = (VA) vdsoStartTmp;
  rinfo->currentVdsoEnd = (VA) vdsoStartTmp + vdsoSize;
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

int
load_mana_header (char *filename, ManaHeader *mh)
{
  int fd = mtcp_sys_open2(filename, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  mtcp_sys_read(fd, &mh->init_flag, sizeof(int));
  mtcp_sys_close(fd);
  return 0;
}

void
set_header_filepath(char* full_filename, char* restartDir)
{
  char *header_filename = "ckpt_rank_0/header.mana";
  char restart_path[PATH_MAX];

  MTCP_ASSERT(mtcp_strlen(header_filename) +
              mtcp_strlen(restartDir) <= PATH_MAX - 2);

  if (mtcp_strlen(restartDir) == 0) {
    mtcp_strcpy(restart_path, "./");
  }
  else {
    mtcp_strcpy(restart_path, restartDir);
    restart_path[mtcp_strlen(restartDir)] = '/';
    restart_path[mtcp_strlen(restartDir)+1] = '\0';
  }
  mtcp_strcpy(full_filename, restart_path);
  mtcp_strncat(full_filename, header_filename, mtcp_strlen(header_filename));
}

bool is_overlap(char *start1, char *end1, char *start2, char *end2) {
  // Check if either memory region is NULL (set to 0).
  if (start1 == NULL || start2 == NULL) {
    return false;
  }
  // Check if the two memory regions are contiguous
  if (end1 < start2 || end2 < start1) {
    return false;
  }
  // The two memory regions overlap if the end of one region is
  //   greater than or equal to the start of the other region
  return end1 >= start2 || end2 >= start1;
}

void populate_plugin_info(RestoreInfo *rinfo)
{
  // FIXME:  Eventually, mpi-proxy-split/mpi_plugin.cpp should
  //         directly write to rinfo->pluginInfo, and we won't
  //         need to do this extra copy, here.
  // Copy the upper-half info to rinfo->pluginInfo

  const char *minLibsStartStr = mtcp_getenv("MANA_MinLibsStart", rinfo->environ);
  if (minLibsStartStr != NULL) {
    rinfo->pluginInfo.minLibsStart = (char*) mtcp_strtoll(minLibsStartStr);
  }

  const char *maxLibsEndStr = mtcp_getenv("MANA_MaxLibsEnd", rinfo->environ);
  if (maxLibsEndStr != NULL) {
    rinfo->pluginInfo.maxLibsEnd = (char*) mtcp_strtoll(maxLibsEndStr);
  }

  const char *minHighMemStartStr = mtcp_getenv("MANA_MinHighMemStart", rinfo->environ);
  if (minHighMemStartStr != NULL) {
    rinfo->pluginInfo.minHighMemStart = (char*) mtcp_strtoll(minHighMemStartStr);
  }

  const char *maxHighMemEndStr = mtcp_getenv("MANA_MaxHighMemEnd", rinfo->environ);
  if (maxHighMemEndStr != NULL) {
    rinfo->pluginInfo.maxHighMemEnd = (char*) mtcp_strtoll(maxHighMemEndStr);
  }

  rinfo->pluginInfo.restartDir = mtcp_getenv("MANA_RestartDir", rinfo->environ);
}

#ifdef SINGLE_CART_REORDER
int
load_cartesian_properties(char *filename, CartesianProperties *cp)
{
  int fd = mtcp_sys_open2(filename, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  mtcp_sys_read(fd, &cp->comm_old_size, sizeof(int));
  mtcp_sys_read(fd, &cp->comm_cart_size, sizeof(int));
  mtcp_sys_read(fd, &cp->comm_old_rank, sizeof(int));
  mtcp_sys_read(fd, &cp->comm_cart_rank, sizeof(int));
  mtcp_sys_read(fd, &cp->reorder, sizeof(int));
  mtcp_sys_read(fd, &cp->ndims, sizeof(int));
  int array_size = sizeof(int) * cp->ndims;
  mtcp_sys_read(fd, cp->coordinates, array_size);
  mtcp_sys_read(fd, cp->dimensions, array_size);
  mtcp_sys_read(fd, cp->periods, array_size);
  mtcp_sys_close(fd);
  return 0;
}

// This function is only for debugging purpose
void
print(CartesianProperties *cp)
{
  int i;
  MTCP_PRINTF("\n\nCartesian Topology Details\n\n");
  MTCP_PRINTF("\nComm Old Size: %d", cp->comm_old_size);
  MTCP_PRINTF("\nComm Cart Size: %d", cp->comm_cart_size);
  MTCP_PRINTF("\nComm Old Rank: %d", cp->comm_old_rank);
  MTCP_PRINTF("\nComm Cart Rank: %d", cp->comm_cart_rank);
  MTCP_PRINTF("\nReorder: %d", cp->reorder);
  MTCP_PRINTF("\nNumber of Dimensions: %d", cp->ndims);
  MTCP_PRINTF("\nCoordinates: ");
  for (i = 0; i < cp->ndims; i++)
    MTCP_PRINTF("%d, ", cp->coordinates[i]);
  MTCP_PRINTF("\nDimensions: ");
  for (i = 0; i < cp->ndims; i++)
    MTCP_PRINTF("%d, ", cp->dimensions[i]);
  MTCP_PRINTF("\nPeriods: ");
  for (i = 0; i < cp->ndims; i++)
    MTCP_PRINTF("%d, ", cp->periods[i]);
  MTCP_PRINTF("\n\n");
}

int
get_rank_corresponding_to_coordinates(int comm_old_size, int ndims, int *coords)
{
  int flag;
  CartesianProperties cp;
  char buffer[10], filename[40];
  for (int i = 0; i < comm_old_size; i++) {
    mtcp_strcpy(filename, "./ckpt_rank_");
    itoa2(i, buffer, 10);
    mtcp_strncat(filename, buffer, 9);
    mtcp_strncat(filename, "/cartesian.info", 20);
    if (load_cartesian_properties(filename, &cp) == 0) {
      flag = 0;
      for (int j = 0; j < ndims; j++) {
        if (cp.coordinates[j] == coords[j]) {
          flag++;
        }
      }
      if (flag == ndims) {
        return cp.comm_old_rank;
      }
    }
  }
  return -1;
}

// This is part of '#ifdef SINGLE_CART_REORDER'
void
mtcp_plugin_hook(RestoreInfo *rinfo)
{
  populate_plugin_info(rinfo);

  remap_vdso_and_vvar_regions(rinfo);
  mysetauxval(rinfo->environ, AT_SYSINFO_EHDR,
              (unsigned long int) rinfo->currentVdsoStart);

  // NOTE: We use mtcp_restart's original stack to initialize the lower
  // half. We need to do this in order to call MPI_Init() in the lower half,
  // which is required to figure out our rank, and hence, figure out which
  // checkpoint image to open for memory restoration.
  // The other assumption here is that we can only handle uncompressed
  // checkpoint images.

  // This creates the lower half and copies the bits, and sets lh_info_addr
  //   to point into the lh_info struct of the lower half:
  splitProcess(rinfo);

  // We need to copy lh_info_addr to a safe place inside rinfo,
  // since mtcp_plugin_skip_memory_region_munmap() is called from
  // mtcp:restorememoryareas(), when we are munmap'ing the old mtcp_restart
  // data segment.  The rinfo struct will be copied onto the stack
  // when calling mtcp:restorememoryareas().
  rinfo->pluginInfo.lh_info_addr = lh_info_addr;

  // Reserve first 500 file descriptors for the Upper-half
  int reserved_fds[500];
  int total_reserved_fds;
  total_reserved_fds = reserve_fds_upper_half(reserved_fds);

  // Refer to "blocked memory" in MANA Plugin Documentation for the addresses
  // We assume that libs and mmap calls grow downward in memory.
  char *start1, *start2, *end1, *end2;
# if 1
  /*
   * FIXME:  Remove '# else' and make this the only branch.
   * if (1 || (rinfo->maxLibsEnd + 1 * GB < rinfo->minHighMemStart)) // end of stack of upper half
   */
  {
    // minLibsStart was chosen with extra space below, for future libs, mmap.
    start1 = rinfo->pluginInfo.minLibsStart;    // first lib of upper half
    // Either lh_info_addr->memRange is a region between 1 GB and 2 GB below
    //   the end of stack in the lower half; or else it is at an unusual
    //   address for which we hope there is no address conflict.
    //   The latter holds if USE_LH_FIXED_ADDRESS was defined in
    //   mtcp_split_process.c, in both restart_plugin and mpi-proxy-split dirs.
    end1 = rinfo->pluginInfo.maxLibsEnd;

    // Reserve 8MB above min high memory region. That should include space for
    // stack, argv, env, auxvec.
    start2 = rinfo->pluginInfo.minHighMemStart - 1 * GB; // Allow for stack to grow
    end2 = start2 + 8 * MB;
    // Ignore region start2:end2 if it is overlapped with region start1:end1
    if (is_overlap(start1, end1, start2, end2)) {
      if (end1 < end2) { end1 = end2; }
      start2 = 0;
      end2 = 0;
    }

    Area vvar_area;
    MTCP_ASSERT(getMappedArea(&vvar_area, "[vvar]") == 1);
    // if (end2 > vvar_area.addr) {
    //   end2 = vvar_area.addr;
    // }

    // ADJUST THE [start2, end2] AROUND THE LOWER-HALF STACK:
    // The lower-half stack is present.  We will restore the upper-half
    // stack later, while restoring the upper half.  We may have an
    // address conflict, since [start2, end2] contains the upper-half
    // stack, and we wish to reserve that memory before allowing MPI_Init
    // to install extra memory (/dev/xpmem, /anon_hugepage, etc.).
    //     We could temporarily move it somewhere else
    // (similar to remap_vdso_and_vvar_regions()), and stop using
    // local variables
    // until we releaseUpperHalfMemoryRegionsForCkptImgs().
    // Instead, we assume MPI_Init will not want to mmap above lh stack.
    Area stack_area;
    MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
    if (is_overlap(start2, end2, stack_area.addr, stack_area.endAddr)) {
      if (start2 < stack_area.addr) {
        if (end2 > stack_area.endAddr) {
          MTCP_PRINTF("*** MANA: uh stack area surrounds lh stack area.\n"
                      "***       Will hope for the best:  See Line %d\n",
                      __LINE__);
        }
        end2 = stack_area.addr;
      } else if (start2 == stack_area.addr) {
        start2 = stack_area.endAddr;
        if (start2 >= end2) { // If we raised start2 by too much:
          start2 = end2 = 0;
        }
      }
    } else if (end1 > stack_area.addr && start1 == 0 && end1 == 0) {
      // Else we merged [start2, end2] into [start1, end1, and now
      // there is an overlap of [start1,end1] with the stack.
      end1 = stack_area.addr;
    }

    // Verify that the lower half and the restored upper half don't conflict.
    if (is_overlap(start1, end1,
                   lh_info_addr->memRange.start,
                   lh_info_addr->memRange.end)) {
      MTCP_PRINTF("uh libs and lower half memRange overlap\n");
      mtcp_abort();
    }
    if (is_overlap(start2, end2,
                   lh_info_addr->memRange.start,
                   lh_info_addr->memRange.end)) {
      MTCP_PRINTF("uh stack and lower half memRange overlap\n");
      mtcp_abort();
    }
  }
# else
   // FIXME:  Remove this '# else' branch when MANA is stable
   else {
    MTCP_PRINTF("*** MANA: Using unstable code in MANA.  Check Line %d\n",
                __LINE__);
    // Allow an extra 1 GB for future upper-half libs and mmaps to be loaded.
    // FIXME:  Will the GNI driver data be allocated below start1?
    //         If so, fix this to block more than a 1 GB address range.
    //         The GNI driver data is only used by Cray GNI.
    //         But lower-half MPI_Init may call additional mmap's not
    //           controlled by us.
    //         Check this logic.
    // NOTE:   setLhMemRange (lh_info_addr->memRange: future mmap's of
    //           lower half was chosen to be in safe memory region
    // NOTE:   When we mmap start1..end1, we will overwrite the text and
    //         data segments of ld.so belonging to mtcp_restart.  But
    //         mtcp_restart is statically linked, and doesn't need it.
    Area heap_area;
    MTCP_ASSERT(getMappedArea(&heap_area, "[heap]") == 1);
    start1 = MAX(heap_area.endAddr, (VA)lh_info_addr->memRange.end);
    Area stack_area;
    MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
    end1 = MIN(stack_area.endAddr - 4 * GB, rinfo->minHighMemStart - 4 * GB);
    // Reserve 8MB above min high memory region. That should include space for
    // stack, argv, env, auxvec.
    start2 = rinfo->minHighMemStart;
    end2 = rinfo->minHighMemStart + 8 * MB;
    // Ignore region start2:end2 if it is overlapped with region start1:end1
    if (is_overlap(start1, end1, start2, end2)) {
      start2 = 0;
      end2 = 0;
    }
  }

  Area vvar_area;
  MTCP_ASSERT(getMappedArea(&vvar_area, "[vvar]") == 1);
  if (end2 > vvar_area.addr) {
    end2 = vvar_area.addr;
  }

  // FIXME:  End of '#if 1'; Remove this '# else' branch when the code is stable
# endif

  reserveUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);

  int rc = -1;
  int world_rank = -1;
  int ckpt_image_rank_to_be_restored = -1;
  char *filename = "./ckpt_rank_0/cartesian.info";

  char full_filename[PATH_MAX];
  set_header_filepath(full_filename, rinfo->pluginInfo.restartDir);
  ManaHeader m_header;
  MTCP_ASSERT(load_mana_header(full_filename, &m_header) == 0);

  MTCP_PRINTF("Initializing with flag: %d\n", m_header.init_flag);

  if (mtcp_sys_access(filename, F_OK) == 0) {
    int coords[MAX_CART_PROP_SIZE];
    CartesianProperties cp;
    MTCP_ASSERT(load_cartesian_properties(filename, &cp) == 0);
# if 0
    print(&CartesianProperties);
# endif
    typedef int (*getCoordinatesFptr_t)(CartesianProperties *, int *, int);
    JUMP_TO_LOWER_HALF(lh_info_addr->fsaddr);
    // MPI_Init is called here. Network memory areas will be loaded by MPI_Init.
    // Also, MPI_Cart_create will be called to restore cartesian topology.
    // Based on the coordinates, checkpoint image will be restored instead of
    // world rank.
    world_rank =
      ((getCoordinatesFptr_t)
      lh_info_addr->getCoordinatesFptr)(&cp, coords, m_header.init_flag);
    RETURN_TO_UPPER_HALF();
# if 0
    MTCP_PRINTF("\nWorld Rank: %d \n: ", world_rank);
    int i;
    MTCP_PRINTF("\nMy Coordinates: ");
    for (i = 0; i < cp.ndims; i++)
      MTCP_PRINTF("%d, ", coords[i]);
# endif
    ckpt_image_rank_to_be_restored =
    get_rank_corresponding_to_coordinates(cp.comm_old_size, cp.ndims, coords, m_header.init_flag);
  } else {
    typedef int (*getRankFptr_t)(void);
    JUMP_TO_LOWER_HALF(lh_info_addr->fsaddr);
    // MPI_Init is called here. Network memory areas will be loaded by MPI_Init.
    world_rank =
      ((getRankFptr_t)lh_info_addr->getRankFptr)(m_header.init_flag);
    RETURN_TO_UPPER_HALF();
    ckpt_image_rank_to_be_restored = world_rank;
  }

  releaseUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
  unreserve_fds_upper_half(reserved_fds, total_reserved_fds);
  MTCP_ASSERT(ckpt_image_rank_to_be_restored != -1);
  if (getCkptImageByDir(rinfo, rinfo->ckptImage, 512,
                        ckpt_image_rank_to_be_restored) == -1) {
    mtcp_strncpy(
      rinfo->ckptImage,
      getCkptImageByRank(ckpt_image_rank_to_be_restored, rinfo->environ),
      PATH_MAX);
  }

  MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n",
              ckpt_image_rank_to_be_restored, rinfo->ckptImage);
}

#else

  // This is the 'else' branch of '#ifdef SINGLE_CART_REORDER'
void
mtcp_plugin_hook(RestoreInfo *rinfo)
{
  populate_plugin_info(rinfo);

  remap_vdso_and_vvar_regions(rinfo);
  mysetauxval(rinfo->environ, AT_SYSINFO_EHDR,
              (unsigned long int) rinfo->currentVdsoStart);

  // NOTE: We use mtcp_restart's original stack to initialize the lower
  // half. We need to do this in order to call MPI_Init() in the lower half,
  // which is required to figure out our rank, and hence, figure out which
  // checkpoint image to open for memory restoration.
  // The other assumption here is that we can only handle uncompressed
  // checkpoint images.

  // This creates the lower half and copies the bits, and sets lh_info_addr
  //   to point into the lh_info struct of the lower half:
  splitProcess(rinfo);

  // We need to copy lh_info_addr to a safe place inside rinfo,
  // since mtcp_plugin_skip_memory_region_munmap() is called from
  // mtcp:restorememoryareas(), when we are munmap'ing the old mtcp_restart
  // data segment.  The rinfo struct will be copied onto the stack
  // when calling mtcp:restorememoryareas().
  rinfo->pluginInfo.lh_info_addr = lh_info_addr;

  // Reserve first 500 file descriptors for the Upper-half
  int reserved_fds[500];
  int total_reserved_fds;
  total_reserved_fds = reserve_fds_upper_half(reserved_fds);

  // Refer to "blocked memory" in MANA Plugin Documentation for the addresses
  // We assume that libs and mmap calls grow downward in memory.
  char *start1, *start2, *end1, *end2;
# if 1
  /*
   * FIXME:  Remove '# else' and make this the only branch.
   * if (1 || (rinfo->maxLibsEnd + 1 * GB < rinfo->minHighMemStart)) // end of stack of upper half
   */
  {
    // minLibsStart was chosen with extra space below, for future libs, mmap.
    start1 = rinfo->pluginInfo.minLibsStart;    // first lib of upper half
    // Either lh_info_addr->memRange is a region between 1 GB and 2 GB below
    //   the end of stack in the lower half; or else it is at an unusual
    //   address for which we hope there is no address conflict.
    //   The latter holds if USE_LH_FIXED_ADDRESS was defined in
    //   mtcp_split_process.c, in both restart_plugin and mpi-proxy-split dirs.
    end1 = rinfo->pluginInfo.maxLibsEnd;

    // Reserve 8MB above min high memory region. That should include space for
    // stack, argv, env, auxvec.
    start2 = rinfo->pluginInfo.minHighMemStart - 1 * GB; // Allow for stack to grow
    end2 = start2 + 8 * MB;
    // Ignore region start2:end2 if it is overlapped with region start1:end1
    if (is_overlap(start1, end1, start2, end2)) {
      if (end1 < end2) { end1 = end2; }
      start2 = 0;
      end2 = 0;
    }

    Area vvar_area;
    MTCP_ASSERT(getMappedArea(&vvar_area, "[vvar]") == 1);
    // if (end2 > vvar_area.addr) {
    //   end2 = vvar_area.addr;
    // }

    // ADJUST THE [start2, end2] AROUND THE LOWER-HALF STACK:
    // The lower-half stack is present.  We will restore the upper-half
    // stack later, while restoring the upper half.  We may have an
    // address conflict, since [start2, end2] contains the upper-half
    // stack, and we wish to reserve that memory before allowing MPI_Init
    // to install extra memory (/dev/xpmem, /anon_hugepage, etc.).
    //     We could temporarily move it somewhere else
    // (similar to remap_vdso_and_vvar_regions()), and stop using
    // local variables
    // until we releaseUpperHalfMemoryRegionsForCkptImgs().
    // Instead, we assume MPI_Init will not want to mmap above lh stack.
    Area stack_area;
    MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
    if (is_overlap(start2, end2, stack_area.addr, stack_area.endAddr)) {
      if (start2 < stack_area.addr) {
        if (end2 > stack_area.endAddr) {
          MTCP_PRINTF("*** MANA: uh stack area surrounds lh stack area.\n"
                      "***       Will hope for the best:  See Line %d\n",
                      __LINE__);
        }
        end2 = stack_area.addr;
      } else if (start2 == stack_area.addr) {
        start2 = stack_area.endAddr;
        if (start2 >= end2) { // If we raised start2 by too much:
          start2 = end2 = 0;
        }
      }
    } else if (end1 > stack_area.addr && start2 == 0 && end2 == 0) {
      // Else we merged [start2, end2] into [start1, end1, and now
      // there is an overlap of [start1,end1] with the stack.
      end1 = stack_area.addr;
    }

    // Verify that the lower half and the restored upper half don't conflict
    if (is_overlap(start1, end1,
                   lh_info_addr->memRange.start,
                   lh_info_addr->memRange.end)) {
      MTCP_PRINTF("uh libs and lower half memRange overlap\n");
      mtcp_abort();
    }
    if (is_overlap(start2, end2,
                   lh_info_addr->memRange.start,
                   lh_info_addr->memRange.end)) {
      MTCP_PRINTF("uh stack and lower half memRange overlap\n");
      mtcp_abort();
    }
  }
# else
   // FIXME:  Remove this '# else' branch when MANA is stable
   else {
    MTCP_PRINTF("*** MANA: Using unstable code in MANA.  Check Line %d\n",
                __LINE__);
    // Allow an extra 1 GB for future upper-half libs and mmaps to be loaded.
    // FIXME:  Will the GNI driver data be allocated below start1?
    //         If so, fix this to block more than a 1 GB address range.
    //         The GNI driver data is only used by Cray GNI.
    //         But lower-half MPI_Init may call additional mmap's not
    //           controlled by us.
    //         Check this logic.
    // NOTE:   setLhMemRange (lh_info_addr->memRange: future mmap's of
    //           lower half was chosen to be in safe memory region
    // NOTE:   When we mmap start1..end1, we will overwrite the text and
    //         data segments of ld.so belonging to mtcp_restart.  But
    //         mtcp_restart is statically linked, and doesn't need it.
    Area heap_area;
    MTCP_ASSERT(getMappedArea(&heap_area, "[heap]") == 1);
    start1 = MAX(heap_area.endAddr, (VA)lh_info_addr->memRange.end);
    Area stack_area;
    MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
    end1 = MIN(stack_area.endAddr - 4 * GB, rinfo->minHighMemStart - 4 * GB);
    // Reserve 8MB above min high memory region. That should include space for
    // stack, argv, env, auxvec.
    MTCP_ASSERT(getMappedArea(&vvar_area, "[vvar]") == 1);
    start2 = rinfo->minHighMemStart;
    end2 = rinfo->minHighMemStart + 8 * MB;
    // Ignore region start2:end2 if it is overlapped with region start1:end1
    if (is_overlap(start1, end1, start2, end2)) {
      start2 = 0;
      end2 = 0;
    }
  }

  Area vvar_area;
  MTCP_ASSERT(getMappedArea(&vvar_area, "[vvar]") == 1);
  if (end2 > vvar_area.addr) {
    end2 = vvar_area.addr;
  }

  // FIXME:  End of '#if 1'; Remove this '# else' branch when the code is stable
# endif

  char full_filename[PATH_MAX];
  set_header_filepath(full_filename, rinfo->pluginInfo.restartDir);
  ManaHeader m_header;
  MTCP_ASSERT(load_mana_header(full_filename, &m_header) == 0);

  typedef int (*getRankFptr_t)(int);
  int rank = -1;
  reserveUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
  JUMP_TO_LOWER_HALF(lh_info_addr->fsaddr);

  // MPI_Init is called here. Networrk memory areas will be loaded by MPI_Init.
  rank = ((getRankFptr_t)lh_info_addr->getRankFptr)(m_header.init_flag);
  RETURN_TO_UPPER_HALF();
  releaseUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
  unreserve_fds_upper_half(reserved_fds,total_reserved_fds);

  if (getCkptImageByDir(rinfo, rinfo->ckptImage, 512, rank) == -1) {
      mtcp_strncpy(rinfo->ckptImage,
      getCkptImageByRank(rank, rinfo->environ),
      PATH_MAX);
  }

  MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, rinfo->ckptImage);
  //ckptImage = getCkptImageByRank(rank, argv);
  //MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, ckptImage);
}
// This is the '#endif' for '#else' of '#ifdef SINGLE_CART_REORDER'
#endif

int
mtcp_plugin_skip_memory_region_munmap(Area *area, RestoreInfo *rinfo)
{
  // lh_proxy lower half is not being unmapped.  lh_info_addr points there.
  LowerHalfInfo_t *lh_info_addr = rinfo->pluginInfo.lh_info_addr;

  LhCoreRegions_t *lh_regions_list = NULL;
  int total_lh_regions = lh_info_addr->numCoreRegions;

  if (regionContains(lh_info_addr->memRange.start,
                     lh_info_addr->memRange.end,
                     area->addr, area->endAddr)) {
    return 1;
  }

  // FIXME: Delete and substitute better code for USE_LH_MMAPS_ARRAY,
  //        that will be aware of mremap, shmat, ioctl, etc.
  //        SEE: mpi-proxy-split/mpi_plugin.cpp for recordPreMpiInitMaps()/Post
#ifdef USE_LH_MMAPS_ARRAY
  MmapInfo_t *g_list = NULL;
#endif
  int *g_numMmaps = NULL;
  int i = 0;

#ifdef USE_LH_MMAPS_ARRAY
  /* FIXME: lower-half tracks two kinds of memory regions:
   *:       (a) libproxy.c:getLhRegionsList() (also called 'core' regions:
   *              the initial mmap'ed regions of the lh_proxy child process)
   *        (b) mmap64.c:getMmappedList() (based on the interposition by
   *              lh_proxy on mmap/munmap (but not mremap, shmat, ioctl, etc.)
   *       We should skip unmapping any of these regions (and more)
   *       when mtcp_restart.c unmaps all regions and restores the upper half.
   *       FIXME:  We need a better scheme:  Maybe based on
   *                 mpi-proxy-split/mpi_plugin.cpp.  We jump to lower-half
   *                 both for initializeLowerHalf() and for mtcp_plugin_hook,
   *                 when it calls MPI_Init via getRank().
   */
  getMmappedList_t fnc = (getMmappedList_t)lh_info_addr->getMmappedListFptr;
  // FIXME: use assert(fnc) instead.
  if (fnc) {
    // This is the mmaps[] array, but only for those regions that are mapped.
    // Now that the __mmap64 wrapper occupies the entire
    //   lh_info_addr->memRange, we should skip unmapping the
    //   entire memRange, and not just the regions of mmaps[].
    g_list = fnc(&g_numMmaps);
  }
#endif

  // NOTE: Check /proc/self/maps after lh_proxy copied over and MPI_INIT
  //       A future O/S may have new "special" memory regoins to add here.
  if (mtcp_strstr(area->name, "/dev/shm/mpich") ||
      mtcp_strstr(area->name, "/dev/zero") ||
      mtcp_strstr(area->name, "/dev/kgni") ||
      mtcp_strstr(area->name, "/dev/xpmem") ||
      mtcp_strstr(area->name, "/dev/shm") ||
      mtcp_strstr(area->name, "/SYS") ||
      mtcp_strstr(area->name, "/anon_hugepage")) {
    return 1;
  }
  if (mtcp_strstr(area->name, "/dev/")) {
    mtcp_printf("*** WARNING: " __FILE__ ": Consider skipping munmap of %s\n",
                area->name);
  }

  // FROM: lh_proxy:mpi-proxy-split/lower-half/libproxy.c :
  //   "For a static LH, mark all the regions till heap as core regions."
  getLhRegionsList_t core_fnc =
                    (getLhRegionsList_t)lh_info_addr->getLhRegionsListFptr;
  if (core_fnc) {
    lh_regions_list = core_fnc(&total_lh_regions);
  }
  if (!lh_regions_list) return 0;
  for (int i = 0; i < total_lh_regions; i++) {
    void *lhStartAddr = lh_regions_list[i].start_addr;
    if (area->addr == lhStartAddr) {
      return 1;
    }
  }

/**
 * InitializeLowerHalf and later we mmap a new lower half memory region which is not one of core region of child lh_proxy.
 * We need to skip unmapping this region as well.
*/
#ifdef USE_LOWER_BOUNDARY
# define LOWER_BOUNDARY 0x1000000000
  if (area->addr >= (char *)lh_regions_list[total_lh_regions - 1].end_addr &&
      area->endAddr <= (char *)LOWER_BOUNDARY) {
    return 1;
  }
#endif

#ifdef USE_LH_MMAPS_ARRAY
  // FIXME: use assert(g_list) instead.
  if (!g_list) return 0;

  for (i = 0; i < *g_numMmaps; i++) {
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
#endif

  return 0;
}
