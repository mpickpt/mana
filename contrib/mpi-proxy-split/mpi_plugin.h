#ifndef _MPI_PLUGIN_H
#define _MPI_PLUGIN_H

#include <mpi.h>
#include <cstdint>

#include "lower_half_api.h"
#include "dmtcp_dlsym.h"

#define   _real_fork      NEXT_FNC_DEFAULT(fork)

#define NOT_IMPLEMENTED(op)                                         \
{                                                                   \
  if (op > MPIProxy_ERROR && op < MPIProxy_Cmd_Shutdown_Proxy)      \
    fprintf(stdout, "[%d:%s] NOT IMPLEMENTED\n", op, __FUNCTION__); \
  else                                                              \
    fprintf(stdout, "[%s] NOT IMPLEMENTED\n", __FUNCTION__);        \
  exit(EXIT_FAILURE);                                               \
}

#define NOT_IMPL(func)                            \
  EXTERNC func                                    \
  {                                               \
    NOT_IMPLEMENTED(MPIProxy_Cmd_Shutdown_Proxy); \
  }

#define PMPI_IMPL(ret, func, ...)                               \
  EXTERNC ret P##func(__VA_ARGS__) __attribute__ ((weak, alias (#func)));

extern struct LowerHalfInfo_t info;
extern int g_numMmaps;
extern MmapInfo_t *g_list;
extern MemRange_t *g_range;

// Pointer to the custom dlsym implementation (see mydlsym() in libproxy.c) in
// the lower half. This is initialized using the information passed to us by
// the transient lh_proxy process in DMTCP_EVENT_INIT.
extern proxyDlsym_t pdlsym;


#endif // ifndef _MPI_PLUGIN_H
