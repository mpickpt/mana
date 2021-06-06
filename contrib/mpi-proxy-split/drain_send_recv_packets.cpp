#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
#include <algorithm>
#include <cassert>
#include <map>
#include <vector>
#include <mutex>

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

using namespace dmtcp;

using mutex_t = std::mutex;
using lock_t  = std::unique_lock<mutex_t>;
using mpi_message_vector_iterator_t =
  dmtcp::vector<mpi_message_t*>::iterator;
using request_to_async_call_map_iterator_t =
  dmtcp::map<MPI_Request* const, mpi_async_call_t*>::iterator;
using request_to_async_call_map_pair_t =
  dmtcp::map<MPI_Request* const, mpi_async_call_t*>::value_type;

static mutex_t srMutex;    // Lock to protect the global 'localSrCount' object
static mutex_t logMutex;   // Lock to protect the global 'g_async_calls' object
static send_recv_totals_t localSrCount; // Stores the counts of local send/recv/unserviced send calls
static vector<send_recv_totals_t> remoteSrCounts; // TODO: Remove this?

static int g_world_rank = -1; // Global rank of the current process
static int g_world_size = -1; // Total number of ranks in the current computation

// Queue of cached messages drained during a checkpoint
static dmtcp::vector<mpi_message_t*> g_message_queue;

// Map of unserviced irecv/isend requests to MPI call params
static dmtcp::map<MPI_Request* const, mpi_async_call_t*> g_async_calls;

// List of unserviced async send requests
static dmtcp::vector<mpi_call_params_t> g_unsvcd_sends;

static void get_remote_sr_counts(uint64_t* , uint64_t* ,
                                 uint64_t* , uint64_t* ,
				 dmtcp::vector<mpi_call_params_t>& );
static bool resolve_async_messages();
static bool drain_packets(const dmtcp::vector<mpi_call_params_t>& );
static bool drain_one_packet(MPI_Datatype datatype, MPI_Comm comm);

void
getLocalRankInfo()
{
  if (g_world_rank == -1) {
    JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &g_world_rank) == MPI_SUCCESS &&
            g_world_rank != -1);
  }
  localSrCount.rank = g_world_rank;
  if (g_world_size == -1) {
    JASSERT(MPI_Comm_size(MPI_COMM_WORLD, &g_world_size) == MPI_SUCCESS &&
            g_world_size != -1);
  }
}

void
drainMpiPackets()
{
  // FIXME: Can we skip the drain and replay here if we were in some
  // collective call? The reasoning is that if we were stuck in some collective
  // call at checkpoint time, any pending sends and received would have been
  // completed. This is because MPI guarantees FIFO ordering on messages.
  dmtcp::vector<mpi_call_params_t> totalUnsvcdSends;
  uint64_t totalSends = 0, totalRecvs = 0, totalSendCount = 0, totalRecvCount = 0;
  int iterations = 0;

  // Get updated info from central db
  get_remote_sr_counts(&totalSends, &totalRecvs, &totalSendCount, &totalRecvCount, totalUnsvcdSends);

  // FIXME: this timeout is a temporary fix, we need to change the way we mark a
  // message serviced or unserviced.
  time_t start_time = time(NULL);
  //FIXME: If this works, delete (totalSends > toalrevcs) condition.
  while (totalSends > totalRecvs && totalSendCount > totalRecvCount) {
    JTRACE("drain mpi packets")(totalSends)(totalRecvs)
          (totalSendCount)(totalRecvCount)(totalUnsvcdSends.size());

    // Drain all unserviced sends
    drain_packets(totalUnsvcdSends);

    // Publish local info with central db
    // We have to call this every time to get up-to-date numbers
    // of all the proxies, since we're not the only one waiting
    registerLocalSendsAndRecvs();

    // TODO: This could be replaced with a dmtcp_global_barrier()
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Barrier)(MPI_COMM_WORLD);
    RETURN_TO_UPPER_HALF();

    // FIXME: see the FIXME before the while loop
    // if we waited more than 2 mins, set the breakout flag to 1.
    // If all ranks agree to breakout, break this while loop and
    // continue checkpointing.
    if (iterations > 120) {
      if (g_world_rank == 0) {
        JNOTE("Drain MPI packets: timeout");
      }
      break;
    }
    if (iterations != 0 && iterations % 20 == 0 && g_world_rank == 0) {
      fprintf(stderr, "Trying to drain MPI network packets; will time out"
              " & ckpt after:\n  %d iterations\n", 120 - iterations);
      fflush(stdout);
    }
    sleep(1);
    iterations++;

    totalSends = totalRecvs = 0;
    totalSendCount = totalRecvCount = 0;
    totalUnsvcdSends.clear();
    // Get updated info from central db
    get_remote_sr_counts(&totalSends, &totalRecvs, &totalSendCount,
                         &totalRecvCount, totalUnsvcdSends);
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

  if (!g_list || g_numMmaps == 0) return;
  o << "/lhregions.dat";
  dmtcp::string fname = o.str();
  int fd = open(fname.c_str(), O_CREAT | O_WRONLY);
#if 0
  // g_range (lh_memory_range) was written for debugging here.
  Util::writeAll(fd, g_range, sizeof(*g_range));
#endif
  Util::writeAll(fd, &g_numMmaps, sizeof(g_numMmaps));
  for (int i = 0; i < g_numMmaps; i++) {
    Util::writeAll(fd, &g_list[i], sizeof(g_list[i]));
  }
  close(fd);
}

void
verifyLocalInfoOnRestart()
{
  int world_rank = -1, world_size = -1;
  JASSERT(g_world_rank != -1 && g_world_size != -1);
  JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &world_rank) == MPI_SUCCESS &&
          world_rank != -1 && world_rank == g_world_rank);
  JASSERT(MPI_Comm_size(MPI_COMM_WORLD, &world_size) == MPI_SUCCESS &&
          world_size != -1 && world_size == g_world_size);
}

void
replayMpiOnRestart()
{
  MPI_Request *request = NULL;
  mpi_async_call_t *message = NULL;
  JTRACE("Replaying unserviced isend/irecv calls");

  for (request_to_async_call_map_pair_t it : g_async_calls) {
    MPI_Status status;
    int retval = 0;
    int flag = 0;
    request = it.first;
    MPI_Request virtRequest = *request;
    message = it.second;

    if (message->serviced)
      continue;

    switch (message->type) {
      case IRECV_REQUEST:
        JTRACE("Replaying Irecv call")(message->params.remote_node);
        retval = MPI_Irecv(message->recvbuf, message->params.count,
                           message->params.datatype,
                           message->params.remote_node,
                           message->params.tag, message->params.comm,
                           request);
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying recv");
        break;
      case ISEND_REQUEST:
        // This case should never happen. All unserviced sends need to be
        // serviced at checkpoint time.
        JWARNING(false).Text("Unexpected unserviced send on restart");
        retval = MPI_Isend(message->sendbuf, message->params.count,
                           message->params.datatype,
                           message->params.remote_node, message->params.tag,
                           message->params.comm, request);
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying send");
        break;
      case IBARRIER_REQUEST:
        JTRACE("Replaying Ibarrier call")(message->params.comm);
        DMTCP_PLUGIN_DISABLE_CKPT();
        MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(message->params.comm);
        JUMP_TO_LOWER_HALF(lh_info.fsaddr);
        retval = NEXT_FUNC(Ibarrier)(realComm, request);
        RETURN_TO_UPPER_HALF();
        UPDATE_REQUEST_MAP(virtRequest, *request);
        *request = virtRequest;
        DMTCP_PLUGIN_ENABLE_CKPT();
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying Ibarrier");
        break;
      case IBCAST_REQUEST:
        JTRACE("Replaying Ibcast call")(message->params.comm);
        retval = MPI_Ibcast(message->recvbuf, message->params.count,
                           message->params.datatype,
                           message->params.root,
                           message->params.comm,
                           request);
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying ibcast");
        break;
      case IREDUCE_REQUEST:
        JTRACE("Replaying Ireduce call")(message->params.comm);
        retval = MPI_Ireduce(message->sendbuf, message->recvbuf,
                           message->params.count,
                           message->params.datatype,
                           message->params.op,
                           message->params.root,
                           message->params.comm,
                           request);
        JASSERT(retval == MPI_SUCCESS).Text("Error while replaying ierduce");
        break;
      default:
        JWARNING(false)(message->type).Text("Unhandled replay call");
        break;
    }
  }
}

void
registerLocalSendsAndRecvs()
{
  static bool firstTime = true;
  bool unsvcdIsends = resolve_async_messages();

  int rc = dmtcp_send_key_val_pair_to_coordinator(MPI_SEND_RECV_DB,
                                                  &(localSrCount.rank),
                                                  sizeof(localSrCount.rank),
                                                  &localSrCount,
                                                  sizeof(localSrCount));
  JWARNING(rc == 1).Text("Error publishing local sends and recvs to the coord");
  if (unsvcdIsends) {
    size_t sz = g_unsvcd_sends.size() * sizeof(g_unsvcd_sends[0]);
    int rc = dmtcp_send_key_val_pair_to_coordinator(MPI_US_DB,
                                                    &(localSrCount.rank),
                                                    sizeof(localSrCount.rank),
                                                    &g_unsvcd_sends[0], sz);
    JWARNING(rc == 1)
            .Text("Error publishing local sends and recvs to the coord");
  }
  if (firstTime) {
    rc = dmtcp_send_key_val_pair_to_coordinator(MPI_WRAPPER_DB,
                                                &(localSrCount.rank),
                                                sizeof(localSrCount.rank),
                                                &g_counts,
                                                sizeof(g_counts));
    JWARNING(rc == 1).Text("Error publishing local wrapper count to the coord");
    firstTime = false;
  }
}

void
updateLocalSends(int count)
{
  // TODO: Use C++ atomics
  lock_t lock(srMutex);
  localSrCount.sends += 1;
  localSrCount.sendCounts += count;
}

void
updateLocalRecvs(int count)
{
  // TODO: Use C++ atomics
  lock_t lock(srMutex);
  localSrCount.recvs += 1;
  localSrCount.recvCounts += count;
}

// For send/recv
void
addPendingRequestToLog(mpi_req_t req, const void* sbuf, void* rbuf, int cnt,
                       MPI_Datatype type, int remote, int tag,
                       MPI_Comm comm, MPI_Request* rq)
{
  mpi_async_call_t *call =
        (mpi_async_call_t*)JALLOC_HELPER_MALLOC(sizeof(mpi_async_call_t));
  call->serviced = false;
  call->type = req;
  call->sendbuf = sbuf;
  call->recvbuf = rbuf;
  call->params.count = cnt;
  call->params.size = -1;  // Uninitialized
  call->params.datatype = type;
  call->params.remote_node = remote;
  call->params.tag = tag;
  call->params.comm = comm;
  call->request = rq;
  call->req = *rq;
  call->flag = false;
  memset(&call->status, 0, sizeof(call->status));
  lock_t lock(logMutex);
  g_async_calls[rq] = call;
}

// For Ibarrier
void
addPendingIbarrierToLog(mpi_req_t req, MPI_Comm comm, MPI_Request* rq)
{
  mpi_async_call_t *call =
        (mpi_async_call_t*)JALLOC_HELPER_MALLOC(sizeof(mpi_async_call_t));
  call->serviced = false;
  call->type = req;
  call->params.comm = comm;
  call->request = rq;
  call->req = *rq;
  call->flag = false;
  memset(&call->status, 0, sizeof(call->status));
  lock_t lock(logMutex);
  g_async_calls[rq] = call;
}

// For Ibcast
void
addPendingIbcastToLog(mpi_req_t req, void* buffer, int cnt,
                      MPI_Datatype type, int root,
                      MPI_Comm comm, MPI_Request* rq)
{
  mpi_async_call_t *call =
        (mpi_async_call_t*)JALLOC_HELPER_MALLOC(sizeof(mpi_async_call_t));
  call->serviced = false;
  call->type = req;
  call->recvbuf = buffer;
  call->params.count = cnt;
  call->params.datatype = type;
  call->params.root = root;
  call->params.comm = comm;
  call->request = rq;
  call->req = *rq;
  call->flag = false;
  memset(&call->status, 0, sizeof(call->status));
  lock_t lock(logMutex);
  g_async_calls[rq] = call;
}

// For Ireduce
void
addPendingIreduceToLog(mpi_req_t req, const void* sbuf, void* rbuf, int cnt,
                       MPI_Datatype type, MPI_Op op, int root,
                       MPI_Comm comm, MPI_Request* rq)
{
  mpi_async_call_t *call =
        (mpi_async_call_t*)JALLOC_HELPER_MALLOC(sizeof(mpi_async_call_t));
  call->serviced = false;
  call->type = req;
  call->sendbuf = sbuf;
  call->recvbuf = rbuf;
  call->params.count = cnt;
  call->params.datatype = type;
  call->params.op = op;
  call->params.root = root;
  call->params.comm = comm;
  call->request = rq;
  call->req = *rq;
  call->flag = false;
  memset(&call->status, 0, sizeof(call->status));
  lock_t lock(logMutex);
  g_async_calls[rq] = call;
}

void
clearPendingRequestFromLog(MPI_Request* req, MPI_Request orig)
{
  lock_t lock(logMutex);
  if (g_async_calls.find(req) != g_async_calls.end()) {
    mpi_async_call_t *call = g_async_calls[req];
    if (call && call->type == IRECV_REQUEST) {
      updateLocalRecvs(call->params.count);
    }
    g_async_calls.erase(req);
    if (call) {
      JALLOC_HELPER_FREE(call);
    }
  }
  // FIXME: Is this doing the same thing as the previous find() above?
  dmtcp::map<MPI_Request* const, mpi_async_call_t*>::iterator iter;
  for (iter = g_async_calls.begin(); iter != g_async_calls.end();) {
    mpi_async_call_t *call = iter->second;
    if (call && call->req == orig) {
      if (call->type == IRECV_REQUEST) {
        updateLocalRecvs(call->params.count);
      }
      JALLOC_HELPER_FREE(call);
      call = NULL;
      // erase() returns the iterator to the next elt
      iter = g_async_calls.erase(iter);
    } else {
      ++iter;
    }
  }
}

bool
isBufferedPacket(int source, int tag, MPI_Comm comm, int *flag,
                 MPI_Status *status, int *retval)
{
  bool ret = false;
  mpi_message_vector_iterator_t req =
    std::find_if(g_message_queue.begin(), g_message_queue.end(),
                 [source, tag, comm](const mpi_message_t *msg)
                 { return ((msg->status.MPI_SOURCE == source) ||
                           (source == MPI_ANY_SOURCE)) &&
                          ((msg->status.MPI_TAG == tag) ||
                           (tag == MPI_ANY_TAG)) &&
                          ((msg->comm == comm)); });
  if (req != std::end(g_message_queue)) {
    *flag = 1;
    *status = (*req)->status;
    ret = true;
  }
  return ret;
}

bool
isServicedRequest(MPI_Request *req, int *flag, MPI_Status *status)
{
  bool ret = false;
  lock_t lock(logMutex);
  request_to_async_call_map_iterator_t it = g_async_calls.find(req);
  if (it != std::end(g_async_calls)) {
    ret = (*it).second->serviced;
    *flag = ret ? 1 : 0;
  }
  return ret;
}

int
consumeBufferedPacket(void *buf, int count, MPI_Datatype datatype,
                      int source, int tag, MPI_Comm comm,
                      MPI_Status *mpi_status, int size)
{
  int cpysize;
  mpi_message_t *foundMsg = NULL;
  mpi_message_vector_iterator_t req =
    std::find_if(g_message_queue.begin(), g_message_queue.end(),
                 [source, tag, comm](const mpi_message_t *msg)
                 { return ((msg->status.MPI_SOURCE == source) ||
                           (source == MPI_ANY_SOURCE)) &&
                          ((msg->status.MPI_TAG == tag) ||
                           (tag == MPI_ANY_TAG)) &&
                          ((msg->comm == comm)); });
  // This should never happen (since the caller should always check first using
  // isBufferedPacket())!
  JASSERT(req != std::end(g_message_queue))(count)(datatype)
         .Text("Unexpected error: no message in the queue matches the given"
               " attributes.");
  foundMsg = *req;

  cpysize = (size < foundMsg->size) ? size: foundMsg->size;
  memcpy(buf, foundMsg->buf, cpysize);
  *mpi_status = foundMsg->status;
  g_message_queue.erase(req);
  JALLOC_HELPER_FREE(foundMsg->buf);
  JALLOC_HELPER_FREE(foundMsg);
  return MPI_SUCCESS;
}

void
resetDrainCounters()
{
  memset(&localSrCount, 0, sizeof(localSrCount));
  g_async_calls.clear();
  g_unsvcd_sends.clear();
}

// Local functions

// Fetches the send/recv data for all the ranks from the coordinator.
// On a successful execution,
//   - 'totalSends' will contain the total number of sends across all the ranks
//   - 'totalRecvs' will contain the total number of recvs across all the ranks
//   - 'unsvcdSends' will contain the metadata of all the unserviced sends for
static void
get_remote_sr_counts(uint64_t *totalSends, uint64_t *totalRecvs,
                     uint64_t *totalSendCount, uint64_t *totalRecvCount,
                     dmtcp::vector<mpi_call_params_t> &unsvcdSends)
{
  void *buf = NULL;
  int len = 0;
  int rc = 0;

  JASSERT(totalRecvs && totalSends && totalSendCount && totalRecvCount).Text("Unexpected NULL arguments");
  rc = dmtcp_send_query_all_to_coordinator(MPI_SEND_RECV_DB, &buf, &len);
  JASSERT(rc == 0)(g_world_rank).Text("Error querying for MPI database.");
  if (len <= 0) {
    JWARNING(false).Text("Could not get remote data from coordinator");
    return;
  }
  char *ptr = (char*)buf;
  while (ptr < (char*)buf + len) {
    // For each rank:
    //   First, we get the send/recv/unserviced send counts
    size_t mysz = sizeof(size_t) + sizeof(int) + sizeof(size_t);
    send_recv_totals_t *sptr = (send_recv_totals_t*)(ptr + mysz);
    *totalSends += sptr->sends;
    *totalRecvs += sptr->recvs;
    *totalSendCount += sptr->sendCounts;
    *totalRecvCount += sptr->recvCounts;
    int ts = sptr->countSends;
    //   Next, if there are unserviced sends, we fetch the metadata of the
    //   unserviced sends ...
    if (ts > 0) {
      JTRACE("Received unserviced requests")(ts);
      uint32_t sz = ts * sizeof(mpi_call_params_t);
      mpi_call_params_t *ps = (mpi_call_params_t*)JALLOC_HELPER_MALLOC(sz);
      rc = dmtcp_send_query_to_coordinator(MPI_US_DB, &sptr->rank,
                                           sizeof(sptr->rank),
                                           ps, &sz);
      JASSERT(rc == sz)(rc)(MPI_US_DB)
             .Text("Error querying for MPI database.");
      // ... and add it to our local list of unserviced sends
      unsvcdSends.insert(std::end(unsvcdSends), ps, ps + ts);
      JALLOC_HELPER_FREE(ps);
    }
    mysz = sizeof(size_t) + sizeof(int) + sizeof(size_t) +
           sizeof(send_recv_totals_t);
    ptr += mysz;
  }
  if (buf) {
    JALLOC_HELPER_FREE(buf);
  }
}

// Returns true if there are unresolved async MPI_Send calls and adds any
// unresolved send requests (i.e., local async MPI sends that haven't yet been
// received by a peer) to the global 'g_unsvcd_sends' queue
static bool
resolve_async_messages()
{
  int unserviced_isends = 0;
  MPI_Request *request;
  mpi_async_call_t *call;

  for (request_to_async_call_map_pair_t it : g_async_calls) {
    MPI_Status status;
    int retval = 0;
    int flag = 0;
    request = it.first;
    call = it.second;

    if (call->serviced)
      continue;

    retval = MPI_Test(request, &call->flag, &call->status);
    JWARNING(retval == MPI_SUCCESS)(call->type)
            .Text("MPI_Test call failed?");
    if (retval == MPI_SUCCESS) {
      if (call->flag) {
        // This information needs to be cached for when the application
        // finally does its own MPI_Test or MPI_Wait on the given MPI_Request
        call->serviced = true;
        JTRACE("Serviced request")(call->type);
        if (call->type == IRECV_REQUEST) {
          // Recv completed successfully => user-specified buffer has data.
          updateLocalRecvs(call->params.count);
        } else if (call->type == ISEND_REQUEST) {
          // Send completed successfully
          g_unsvcd_sends.erase(std::remove_if(g_unsvcd_sends.begin(),
                                              g_unsvcd_sends.end(),
                                              [call](const mpi_call_params_t &p)
                                              {
                                                return memcmp(&p,
                                                              &call->params,
                                                              sizeof(p)) == 0;
                                              }));
        }
      } else {
        JTRACE("Unserviced request")(call->type);
        // FIXME: Why ISEND_REQUEST is checked twice?
        if (call->type == ISEND_REQUEST) {
          unserviced_isends++;
          g_unsvcd_sends.push_back(call->params);
          localSrCount.countSends++;
        }
      }
    }
  }
  return unserviced_isends != 0;
}

// Drains (i.e., executes non-blocking receives for) all unserviced packets in
// the given 'unsvcdSends' queue
static bool
drain_packets(const dmtcp::vector<mpi_call_params_t> &unsvcdSends)
{
  bool ret = true;
  for (mpi_call_params_t it : unsvcdSends) {
    ret &= drain_one_packet(it.datatype, it.comm);
  }
  return ret;
}

// Drains (i.e., executes non-blocking receives for) a single unserviced
// packet with the given 'datatype' and using the given 'comm' MPI communicator.
// The received packet is saved to the global 'g_message_queue'.
static bool
drain_one_packet(MPI_Datatype datatype, MPI_Comm comm)
{
  void *buf = NULL;
  int size = 0;
  int count = 0;
  int flag = 0;
  MPI_Status status;
  mpi_message_t *message;

  // Probe for waiting packet
  int rc = MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &flag, &status);

  if (rc != MPI_SUCCESS || !flag) {
    return false;
  }

  // There's a packet waiting for us
  rc = MPI_Get_count(&status, MPI_BYTE, &count);
  if (rc != MPI_SUCCESS) {
    JWARNING(false).Text("[DRAIN] Failed to get count of elements of packet.");
    return false;
  }

  // Get Type_size info
  // FIXME: get actual type size
  rc = MPI_Type_size(MPI_BYTE, &size);
  if (rc != MPI_SUCCESS) {
    JWARNING(false).Text("[DRAIN] Failed to get size of MPI_BYTE.");
    return false;
  }

  // allocate our receive buffer
  size = count * size;
  buf = JALLOC_HELPER_MALLOC(size); // maximum of 65535 ints

  // drain from lh_proxy to plugin buffer
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  rc = NEXT_FUNC(Recv)(buf, count, MPI_BYTE, status.MPI_SOURCE,
                       status.MPI_TAG, realComm, &status);
  RETURN_TO_UPPER_HALF();
  if (rc != MPI_SUCCESS) {
    JWARNING(false).Text("[DRAIN] Failed to receive packet.");
    return false;
  }

  // copy all data into local message
  message = (mpi_message_t *)JALLOC_HELPER_MALLOC(sizeof(mpi_message_t));
  message->buf        = buf;
  message->count      = count;
  message->datatype   = MPI_BYTE;
  message->comm       = comm;
  message->status     = status;
  message->size       = size;

  // queue it
  g_message_queue.push_back(message);
  updateLocalRecvs(count);

  return true;
}
