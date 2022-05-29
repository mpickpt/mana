#include <asm/prctl.h>
#define _GNU_SOURCE // needed for MREMAP_MAYMOVE
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/auxv.h>
#include "mtcp_restart.h"
#include "mtcp_sys.h"
#include "mtcp_util.h"
#include "mtcp_split_process.h"

# define GB (uint64_t)(1024 * 1024 * 1024)
#define ROUNDADDRUP(addr, size) ((addr + size - 1) & ~(size - 1))

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
int getCkptImageByDir(RestoreInfo *rinfo, char *buffer, size_t buflen, int rank) {
  if(!rinfo->restartDir) {
    MTCP_PRINTF("***ERROR No restart directory found - cannot find checkpoint image by directory!");
    return -1;
  }

  size_t len = mtcp_strlen(rinfo->restartDir);
  if(len >= buflen){
    MTCP_PRINTF("***ERROR Restart directory would overflow given buffer!");
    return -1;
  }
  mtcp_strcpy(buffer, rinfo->restartDir); // start with directory

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
  Area area;
  void *rc = NULL;
  uint64_t vvarStart = (uint64_t) rinfo->currentVvarStart;
  uint64_t vvarSize = rinfo->currentVvarEnd - rinfo->currentVvarStart;
  uint64_t vdsoStart = (uint64_t) rinfo->currentVdsoStart;
  uint64_t vdsoSize = rinfo->currentVdsoEnd - rinfo->currentVdsoStart;
  uint64_t prev_addr = 0x10000;

  uint64_t vvarStartTmp = 0;
  uint64_t vdsoStartTmp = 0;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (prev_addr + vvarSize + vdsoSize <= (uint64_t) area.addr) {
      vvarStartTmp = prev_addr;
      vdsoStartTmp = prev_addr + vvarSize;
      break;
    } else {
      prev_addr = ROUNDADDRUP((uint64_t) area.endAddr, MTCP_PAGE_SIZE);
    }
  }

  mtcp_sys_close(mapsfd);

  if (vvarStart > 0) {
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

void
mtcp_plugin_hook(RestoreInfo *rinfo)
{
  remap_vdso_and_vvar_regions(rinfo);
  mysetauxval(rinfo->environ, AT_SYSINFO_EHDR,
              (unsigned long int) rinfo->currentVdsoStart);

  // NOTE: We use mtcp_restart's original stack to initialize the lower
  // half. We need to do this in order to call MPI_Init() in the lower half,
  // which is required to figure out our rank, and hence, figure out which
  // checkpoint image to open for memory restoration.
  // The other assumption here is that we can only handle uncompressed
  // checkpoint images.

  // This creates the lower half and copies the bits to this address space
  splitProcess(rinfo);

  // Reserve first 500 file descriptors for the Upper-half
  int reserved_fds[500];
  int total_reserved_fds;
  total_reserved_fds = reserve_fds_upper_half(reserved_fds);

  // Refer to "blocked memory" in MANA Plugin Documentation for the addresses
  // FIXME:  Rewrite this logic more carefully.
  char *start1, *start2, *end1, *end2;
  if (rinfo->maxLibsEnd + 1 * GB < rinfo->minHighMemStart /* end of stack of upper half */) {
    start1 = rinfo->minLibsStart;    // first lib (ld.so) of upper half
    // lh_info.memRange is the memory region for the mmaps of the lower half.
    // Apparently lh_info.memRange is a region between 1 GB and 2 GB below
    //   the end of stack in the lower half.
    // One gigabyte below those mmaps of the lower half, we are creating space
    //   for the GNI driver data, to be created when the GNI library runs.
    // This says that we have to reserve only up to the mtcp_restart stack.
    end1 = rinfo->pluginInfo.memRange.start - 1 * GB;
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
    start1 = MAX(heap_area.endAddr, (VA)rinfo->pluginInfo.memRange.end);
    Area stack_area;
    MTCP_ASSERT(getMappedArea(&stack_area, "[stack]") == 1);
    end1 = MIN(stack_area.endAddr - 4 * GB, rinfo->minHighMemStart - 4 * GB);
    start2 = 0;
    end2 = start2;
  }

  typedef int (*getRankFptr_t)(void);
  int rank = -1;
  reserveUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
  JUMP_TO_LOWER_HALF(rinfo->pluginInfo.fsaddr);
  
  // MPI_Init is called here. GNI memory areas will be loaded by MPI_Init.
  rank = ((getRankFptr_t)rinfo->pluginInfo.getRankFptr)();
  RETURN_TO_UPPER_HALF();
  releaseUpperHalfMemoryRegionsForCkptImgs(start1, end1, start2, end2);
  unreserve_fds_upper_half(reserved_fds,total_reserved_fds);

  if(getCkptImageByDir(rinfo, rinfo->ckptImage, 512, rank) == -1) {
      mtcp_strncpy(rinfo->ckptImage,  getCkptImageByRank(rank, rinfo->argv), PATH_MAX);
  }

  MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, rinfo->ckptImage);
  //ckptImage = getCkptImageByRank(rank, argv);
  //MTCP_PRINTF("[Rank: %d] Choosing ckpt image: %s\n", rank, ckptImage);
}

int
mtcp_plugin_skip_memory_region_munmap(Area *area, RestoreInfo *rinfo)
{
  LowerHalfInfo_t *lh_info = &rinfo->pluginInfo;
  LhCoreRegions_t *lh_regions_list = NULL;
  int total_lh_regions = lh_info->numCoreRegions;

  MmapInfo_t *g_list = NULL;
  int g_numMmaps = 0;
  int i = 0;

  getMmappedList_t fnc = (getMmappedList_t)lh_info->getMmappedListFptr;
  // FIXME: use assert(fnc) instread.
  if (fnc) {
    g_list = fnc(&g_numMmaps);
  }
  if (mtcp_strstr(area->name, "/dev/shm/mpich") ||
      mtcp_strstr(area->name, "/dev/zero") ||
      mtcp_strstr(area->name, "/dev/kgni") ||
      mtcp_strstr(area->name, "/dev/xpmem") ||
      mtcp_strstr(area->name, "/dev/shm") ||
      mtcp_strstr(area->name, "/SYS")) {
    return 1;
  }

  getLhRegionsList_t core_fnc = (getLhRegionsList_t)lh_info->getLhRegionsListFptr;
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

  // FIXME: use assert(g_list) instread.
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
