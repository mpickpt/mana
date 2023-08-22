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

typedef LowerHalfInfo_t PluginInfo;

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
