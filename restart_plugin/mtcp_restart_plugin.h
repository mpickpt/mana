#ifndef __MTCP_RESTART_PLUGIN_H__
# define __MTCP_RESTART_PLUGIN_H__

// This part is specific to lh_proxy

#include <lower_half_api.h>

// This part is specific to the DMTCP/MTCP restart plugin.

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
