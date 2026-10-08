/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
#include <pthread.h>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include <execinfo.h>

#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"

#include "mpi_plugin.h"
#include "mpi_nextfunc.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "virtual_id.h"

using namespace dmtcp;

std::unordered_map<MPI_Request, request_info_t*> request_log;
int g_world_rank = -1; // Global rank of the current process
int g_world_size = -1; // Total number of ranks in the current computation
// Mutex protecting request_log
static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;

// The pending MPI_Isend/MPI_Irecv calls, stored in their requests'
// virtual-ID entries and linked in posting order.  Application threads and
// the checkpoint thread (P2P drain, restart) both change the list.  The
// spinlock pendingLock guards the links and call types; never hold it across
// an MPI call.
static virt_id_entry *pendingHead = NULL;
static virt_id_entry *pendingTail = NULL;
static int pendingLock = 0;

static inline void
lockPending()
{
  while (__atomic_exchange_n(&pendingLock, 1, __ATOMIC_ACQUIRE)) {
    while (__atomic_load_n(&pendingLock, __ATOMIC_RELAXED)) {
    }
  }
}

static inline void
unlockPending()
{
  __atomic_store_n(&pendingLock, 0, __ATOMIC_RELEASE);
}

void
getLocalRankInfo()
{
  if (g_world_rank == -1) {
    JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &g_world_rank) == MPI_SUCCESS &&
        g_world_rank != -1);
  }
  if (g_world_size == -1) {
    JASSERT(MPI_Comm_size(MPI_COMM_WORLD, &g_world_size) == MPI_SUCCESS &&
        g_world_size != -1);
  }
}

void
updateCkptDirByRank()
{
  const char *ckptDir = dmtcp_get_ckpt_dir();
  dmtcp::string baseDir;

  if (strstr(ckptDir, "ckpt_rank_") != NULL) {
    baseDir = jalib::Filesystem::DirName(ckptDir);
  } else {
    baseDir = ckptDir;
  }
  JTRACE("Updating checkpoint directory")(ckptDir)(baseDir);
  dmtcp::ostringstream o;
  o << baseDir << "/ckpt_rank_" << g_world_rank;
  dmtcp_set_ckpt_dir(o.str().c_str());

#if 0
  o << "/lhregions.dat";
  dmtcp::string fname = o.str();
  int fd = open(fname.c_str(), O_CREAT | O_WRONLY, 0600);
  // g_range (lh_memory_range) was written for debugging here.
  Util::writeAll(fd, g_range, sizeof(*g_range));
  close(fd);
#endif
}

void
logRequestInfo(MPI_Request request, mpi_req_t req_type)
{
  request_info_t *req_info;
  std::unordered_map<MPI_Request, request_info_t*>::iterator it;
  it = request_log.find(request);
  if (it != request_log.end()) {
    // Update existing request
    req_info = it->second;
    if (req_info->update_counter <= REAL_REQUEST_LOG_LEVEL) {
      req_info->update_counter++;
      req_info->real_request[req_info->update_counter] =
        get_real_id((mana_mpi_handle){.request = request}).request;
    } else {
      JWARNING(false).Text("Too many real request update");
    }
  } else {
    // Create new request log
    req_info =
      (request_info_t*)JALLOC_HELPER_MALLOC(sizeof(request_info_t));
    memset(&req_info->real_request, 0,
        sizeof(MPI_Request) * REAL_REQUEST_LOG_LEVEL);
    memset(&req_info->backtrace, 0, sizeof(void*) * STACK_TRACK_LEVEL);
    req_info->type = req_type;
    req_info->real_request[0] = get_real_id((mana_mpi_handle){.request = request}).request;
    req_info->update_counter = 0;
    backtrace(&(req_info->backtrace[0]), STACK_TRACK_LEVEL);
    pthread_mutex_lock(&logMutex);
    request_log[request] = req_info;
    pthread_mutex_unlock(&logMutex);
  }
}

request_info_t*
lookupRequestInfo(MPI_Request request)
{
  request_info_t *req_info;
  std::unordered_map<MPI_Request, request_info_t*>::iterator it;
  it = request_log.find(request);
  if (it != request_log.end()) {
    return it->second;
  } else {
    return NULL;
  }
}

void
addPendingRequestToLog(mpi_req_t req, const void* sbuf, void* rbuf, int cnt,
                       MPI_Datatype type, int remote, int tag,
                       MPI_Comm comm, MPI_Request rq)
{
  virt_id_entry *entry = get_virt_id_entry((mana_mpi_handle){.request = rq});
  mpi_nonblocking_call_t *call = &entry->call;
  call->sendbuf = sbuf;
  call->recvbuf = rbuf;
  call->count = cnt;
  call->datatype = type;
  call->remote_node = remote;
  call->tag = tag;
  call->comm = comm;
  lockPending();
  call->type = req;
  entry->pending_prev = pendingTail;
  entry->pending_next = NULL;
  if (pendingTail != NULL) {
    pendingTail->pending_next = entry;
  } else {
    pendingHead = entry;
  }
  pendingTail = entry;
  unlockPending();
}

void
clearPendingRequestFromLog(MPI_Request req)
{
  // Look the request up under the lock: while the P2P drain clears it, the
  // application may free it and a new request may take its slot.
  lockPending();
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = req});
  if (entry == NULL) {
    unlockPending();
    return;
  }
  if (entry->call.type != UNKNOW_REQUEST) {
    if (entry->pending_prev != NULL) {
      entry->pending_prev->pending_next = entry->pending_next;
    } else {
      pendingHead = entry->pending_next;
    }
    if (entry->pending_next != NULL) {
      entry->pending_next->pending_prev = entry->pending_prev;
    } else {
      pendingTail = entry->pending_prev;
    }
    entry->pending_prev = NULL;
    entry->pending_next = NULL;
    entry->call.type = UNKNOW_REQUEST;
  }
  unlockPending();
}

void
replacePendingCall(MPI_Request req, const mpi_nonblocking_call_t *call)
{
  lockPending();
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = req});
  JASSERT(entry != NULL && entry->call.type != UNKNOW_REQUEST)(req);
  entry->call = *call;
  unlockPending();
}

std::vector<MPI_Request>
pendingRequestsInPostingOrder()
{
  std::vector<MPI_Request> requests;
  lockPending();
  for (virt_id_entry *entry = pendingHead; entry != NULL;
       entry = entry->pending_next) {
    requests.push_back((MPI_Request)entry->virt);
  }
  unlockPending();
  return requests;
}

mpi_req_t
pendingRequestType(MPI_Request req)
{
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = req});
  if (entry == NULL) {
    return UNKNOW_REQUEST;
  }
  return __atomic_load_n(&entry->call.type, __ATOMIC_RELAXED);
}

bool
getPendingCall(MPI_Request req, mpi_nonblocking_call_t *call)
{
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = req});
  if (entry == NULL) {
    return false;
  }
  lockPending();
  *call = entry->call;
  unlockPending();
  return call->type != UNKNOW_REQUEST;
}

bool
pendingCallUsesDatatype(MPI_Datatype type)
{
  bool found = false;
  lockPending();
  for (virt_id_entry *entry = pendingHead; entry != NULL;
       entry = entry->pending_next) {
    if (entry->call.datatype == type) {
      found = true;
      break;
    }
  }
  unlockPending();
  return found;
}

void
replayMpiP2pOnRestart()
{
  MPI_Request request;
  mpi_nonblocking_call_t pendingCall;
  mpi_nonblocking_call_t *call = &pendingCall;
  JTRACE("Replaying unserviced isend/irecv calls");

  // No other thread runs at restart; a lock saved in the image is stale.
  pendingLock = 0;
  // Re-post the receives in the order they were posted.
  for (MPI_Request pending : pendingRequestsInPostingOrder()) {
    int retval = 0;
    request = pending;
    if (!getPendingCall(request, call)) {
      continue;
    }
    MPI_Comm realComm = get_real_id((mana_mpi_handle){.comm = call->comm}).comm;
    MPI_Datatype realType = get_real_id((mana_mpi_handle){.datatype = call->datatype}).datatype;
    MPI_Request realRequest;
    switch (call->type) {
      case IRECV_REQUEST:
        JTRACE("Replaying Irecv call")(call->remote_node);
#if 0
        MPI_Irecv(call->recvbuf, call->count,
                  realType, call->remote_node,
                  call->tag, realComm, &request);
#else
        JUMP_TO_LOWER_HALF(lh_info->fsaddr);
        NEXT_FUNC(Irecv)(call->recvbuf, call->count,
                         realType, call->remote_node,
                         call->tag, realComm, &realRequest);
        RETURN_TO_UPPER_HALF();
        update_virt_id((mana_mpi_handle){.request = request}, (mana_mpi_handle){.request = realRequest});
#endif
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying recv");
        break;
      case ISEND_REQUEST:
      case ISSEND_REQUEST:
        JASSERT(false)(call->type)
          .Text("There should be no pending MPI_Isend after restart");
        break;
      default:
        JWARNING(false)(call->type).Text("Unhandled replay call");
        break;
    }
  }
}
