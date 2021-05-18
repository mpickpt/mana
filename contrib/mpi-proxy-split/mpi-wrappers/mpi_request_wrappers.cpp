#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "drain_send_recv_packets.h"
#include "mpi_plugin.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"

EXTERNC
USER_DEFINED_WRAPPER(int, Test, (MPI_Request*) request,
                     (int*) flag, (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  if (isServicedRequest(request, flag, status)) {
    DMTCP_PLUGIN_ENABLE_CKPT();
  } else {
    MPI_Request req = *request;
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    // MPI_Test can change the *request argument
    retval = NEXT_FUNC(Test)(request, flag, status);
    RETURN_TO_UPPER_HALF();
    if (retval == MPI_SUCCESS && *flag) {
      clearPendingRequestFromLog(request, req);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Testall, (int) count, (MPI_Request*) requests,
                     (int*) flag, (MPI_Status*) statuses)
{
  int retval;
  bool incomplete = false;
  // FIXME: Perhaps use Testall directly? But then, need to take care of
  // the services requests
  for (int i = 0; i < count; i++) {
    if (statuses != MPI_STATUSES_IGNORE) {
      retval = MPI_Test(&requests[i], flag, &statuses[i]);
    } else {
      retval = MPI_Test(&requests[i], flag, MPI_STATUS_IGNORE);
    }
    if (retval != MPI_SUCCESS) {
      *flag = 0;
      break;
    }
    if (*flag == 0) {
      incomplete = true;
    }
  }
  if (incomplete) {
    *flag = 0;
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Waitall, (int) count,
                     (MPI_Request *) array_of_requests,
                     (MPI_Status *) array_of_statuses)
{
  // FIXME: Revisit this wrapper
  int retval;
#if 0
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Waitall)(count, array_of_requests, array_of_statuses);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS) {
    for (int i = 0; i < count; i++) {
      clearPendingRequestFromLog(&array_of_requests[i]);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
#else
  for (int i = 0; i < count; i++) {
    if (array_of_statuses != MPI_STATUS_IGNORE) {
      retval = MPI_Wait(&array_of_requests[i], &array_of_statuses[i]);
    } else {
      retval = MPI_Wait(&array_of_requests[i], MPI_STATUS_IGNORE);
    }
    if (retval != MPI_SUCCESS) {
      break;
    }
  }
#endif
  return retval;
}

USER_DEFINED_WRAPPER(int, Wait, (MPI_Request*) request, (MPI_Status*) status)
{
  int retval;
  int flag = 0;
  while (!flag) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    if (isServicedRequest(request, &flag, status)) {
      DMTCP_PLUGIN_ENABLE_CKPT();
      JWARNING(flag).Text("Unexpected buffered-yet-unserviced packet.");
      continue;
    }
    MPI_Request req = *request;
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Test)(request, &flag, status);
    RETURN_TO_UPPER_HALF();
    if (flag) {
      clearPendingRequestFromLog(request, req);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Probe, (int) source, (int) tag,
                     (MPI_Comm) comm, (MPI_Status *) status)
{
  int retval;
  int flag = 0;
  while (!flag) {
    retval = MPI_Iprobe(source, tag, comm, &flag, status);
  }
  return retval;
}

USER_DEFINED_WRAPPER(int, Iprobe,
                     (int) source, (int) tag, (MPI_Comm) comm, (int*) flag,
                     (MPI_Status *) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();

  if (isBufferedPacket(source, tag, comm, flag, status, &retval)) {
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  }

  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Iprobe)(source, tag, realComm, flag, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Request_get_status, (MPI_Request) request,
                     (int*) flag, (MPI_Status*) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  if (isServicedRequest(&request, flag, status)) {
    DMTCP_PLUGIN_ENABLE_CKPT();
  } else {
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Request_get_status)(request, flag, status);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
  }
  return retval;
}


PMPI_IMPL(int, MPI_Test, MPI_Request* request, int* flag, MPI_Status* status)
PMPI_IMPL(int, MPI_Wait, MPI_Request* request, MPI_Status* status)
PMPI_IMPL(int, MPI_Iprobe, int source, int tag, MPI_Comm comm, int *flag,
          MPI_Status *status)
PMPI_IMPL(int, MPI_Probe, int source, int tag,
          MPI_Comm comm, MPI_Status *status)
PMPI_IMPL(int, MPI_Waitall, int count, MPI_Request array_of_requests[],
          MPI_Status *array_of_statuses)
PMPI_IMPL(int, MPI_Testall, int count, MPI_Request *requests,
          int *flag, MPI_Status *statuses)
PMPI_IMPL(int, MPI_Request_get_status, MPI_Request request, int* flag,
          MPI_Status* status)
