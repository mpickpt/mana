#ifndef __MTCP_RESTART_PLUGIN_H__
#define __MTCP_RESTART_PLUGIN_H__
#include <sys/types.h>
#include <stdint.h>
#include "config.h"

#define MAX_LH_REGIONS 500

typedef void (*fnptr_t)();

// FIXME: Much of this is duplicated in ../lower-half/lower_half_api.h

typedef struct __MemRange
{
  void *start;
  void *end;
} MemRange_t;

typedef struct __MmapInfo
{
  void *addr;
  size_t len;
  int unmapped;
  int guard;
} MmapInfo_t;

typedef struct __LhCoreRegions
{
  void *start_addr; // Start address of a LH memory segment
  void *end_addr; // End address
  int prot; // Protection flag
} LhCoreRegions_t;

// The transient proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
typedef struct LowerHalfInfo
{
  void *startText;
  void *endText;
  void *endOfHeap;
  void *libc_start_main;
  void *main;
  void *libc_csu_init;
  void *libc_csu_fini;
  void *fsaddr;
  uint64_t lh_AT_PHNUM;
  uint64_t lh_AT_PHDR;
  void *g_appContext;
  void *lh_dlsym;
  void *getRankFptr;
#ifdef SINGLE_CART_REORDER
  void *getCoordinatesFptr;
  void *getCartesianCommunicatorFptr;
#endif
  void *parentStackStart;
  void *updateEnvironFptr;
  void *getMmappedListFptr;
  void *resetMmappedListFptr;
  int numCoreRegions;
  void *getLhRegionsListFptr;
  void *vdsoLdAddrInLinkMap;
  MemRange_t memRange;
} LowerHalfInfo_t;

extern LowerHalfInfo_t *lh_info_addr;

typedef struct PluginInfo {
  size_t size;
  LowerHalfInfo_t *lh_info_addr;
  // FIXME: Remove MANA-specific code from DMTCP:
  //        Eventually, remove these fields from dmtcp/src/mtcp/mtcp_restart.h,
  //        and use the fields only here.  
  VA minLibsStart;
  VA maxLibsEnd;
  VA minHighMemStart;
  VA maxHighMemEnd;
  char* restartDir;
} PluginInfo_t;

// FIXME:  This 'PluginInfo' is required by dmtcp/src/mtcp/mtcp_restart.h.
//         It might be simpler, if dmtcp/src/mtcp/mtcp_restart.h.
//           did not include this file, and simply declares
//           PluginInfo to be 'char[512]' as a maximum size.
//           Then we can directly do a cast '(PluginInfo_t)rinfo.pluginInfo'
//           in the local code of this directory.
//         We could remove the '512' constant from dmtcp/src/mtcp/mtcp_restart.h
//           by using a rinfo.pluginInfoSize field and rinfo.pluginInfoAddr
//           field, to read in a separate PluginInfo header.  Then, when
//           the upper half writes its info into the ckpt image header,
//           it will write the size, and then write into the header
//           a separate PluginInfo of that size.
//         After that, we can remove this line definig 'PluginInfo' for MTCP.
typedef PluginInfo_t PluginInfo;

typedef struct RestoreInfo RestoreInfo;
union ProcMapsArea;
void mtcp_plugin_hook(RestoreInfo *rinfo);
int mtcp_plugin_skip_memory_region_munmap(ProcMapsArea *area,
                                          RestoreInfo *rinfo);
int getCkptImageByDir(RestoreInfo *rinfo,
                      char *buffer,
                      size_t buflen,
                      int rank);
char* getCkptImageByRank(int rank, char **argv);

#endif // #ifndef __MTCP_RESTART_PLUGIN_H__
