#include <stdio.h>
#include <mpi.h>
#include <map>
#include <algorithm>
#include <vector>
#include "jassert.h"
#include "p2p_drain_send_recv.h"
#include "p2p_log_replay.h"

extern int MPI_Comm_create_group_internal(MPI_Comm comm, MPI_Group group,
                                          int tag, MPI_Comm *newcomm);
extern int MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype,
                                 MPI_Comm comm);
extern int MPI_Comm_free_internal(MPI_Comm *comm);
extern int MPI_Group_free_internal(MPI_Group *group);
int *g_sendBytesByRank; // Number of bytes sent to other ranks
int *g_bytesSentToUsByRank; // Number of bytes other ranks sent to us
int *g_recvBytesByRank; // Number of bytes received from other ranks
std::unordered_set<MPI_Comm> active_comms;
dmtcp::vector<mpi_message_t*> g_message_queue;

void
initialize_drain_send_recv()
{
  getLocalRankInfo();
  g_sendBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_bytesSentToUsByRank =
    (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  g_recvBytesByRank = (int*)JALLOC_HELPER_MALLOC(g_world_size * sizeof(int));
  active_comms.insert(MPI_COMM_WORLD);
  active_comms.insert(MPI_COMM_SELF);
}

void
registerLocalSendsAndRecvs()
{
  // Get a copy of MPI_COMM_WORLD
  MPI_Group group_world;
  MPI_Comm mana_comm;
  MPI_Comm_group(MPI_COMM_WORLD, &group_world);
  MPI_Comm_create_group_internal(MPI_COMM_WORLD, group_world, 1, &mana_comm);

  // broadcast sendBytes and recvBytes
  MPI_Alltoall_internal(g_sendBytesByRank, 1, MPI_INT,
                        g_bytesSentToUsByRank, 1, MPI_INT, mana_comm);
  g_bytesSentToUsByRank[g_world_rank] = 0;

  // Free resources
  MPI_Comm_free_internal(&mana_comm);
  MPI_Group_free_internal(&group_world);
}

// status was received by MPI_Iprobe
int
recvMsgIntoInternalBuffer(MPI_Status status, MPI_Comm comm)
{
  int count = 0;
  int size = 0;
  MPI_Get_count(&status, MPI_BYTE, &count);
  MPI_Type_size(MPI_BYTE, &size);
  JASSERT(size == 1);
  void *buf = JALLOC_HELPER_MALLOC(count);
  MPI_Recv(buf, count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG,
           comm, MPI_STATUS_IGNORE);

  mpi_message_t *message = (mpi_message_t *)JALLOC_HELPER_MALLOC(sizeof(mpi_message_t));
  message->buf        = buf;
  message->count      = count;
  message->datatype   = MPI_BYTE;
  message->comm       = comm;
  message->status     = status;
  message->size       = size * count;

  // queue it
  g_message_queue.push_back(message);

  return count;
}

int
recvFromAllComms(int source)
{
  int bytesReceived = 0;
  std::unordered_set<MPI_Comm>::iterator comm;
  for (comm = active_comms.begin(); comm != active_comms.end(); comm++) {
    if (*comm == MPI_COMM_SELF && source != g_world_rank) {
      continue;
    }
    int flag;
    MPI_Status status;
    MPI_Iprobe(source, MPI_ANY_TAG, *comm, &flag, &status);

    if (flag) {
      bytesReceived += recvMsgIntoInternalBuffer(status, *comm);
    }
  }
  return bytesReceived;
}

void
removePendingSendRequests()
{
  dmtcp::map<MPI_Request, mpi_async_call_t*>::iterator it;
  for (it = g_async_calls.begin(); it != g_async_calls.end(); it++) {
    MPI_Request request = it->first;
    mpi_async_call_t *call = it->second;
    int flag = 0;
    if (call->type == ISEND_REQUEST) {
      // All pending MPI_Isend request should be true at this point
      MPI_Test(&request, &flag, MPI_STATUS_IGNORE);
      JASSERT(flag)(request)(flag);
    }
  }
}

void
drainSendRecv()
{
  int timeout_counter = 0;
  bool allDrained = false;
  while (!allDrained) {
    allDrained = true;
    int i;
    for (i = 0; i < g_world_size; i++) {
      if (g_bytesSentToUsByRank[i] > g_recvBytesByRank[i]) {
        allDrained = false;
        sleep(1);
        int numReceived = recvFromAllComms(i);
        // JASSERT(numReceived != 0)(numReceived);
        // recvFromAllComms will call the MPI_Recv wrapper, MPI_Recv wrapper
        // call MPI_Test wrapper, and MPI_Test wrapper will update
        // g_recvBytesByRank[i].
      }
    }
    // if we finished many rounds and we are not receiving more,
    // print a warning message with fprintf(stderr).
    if (timeout_counter > 20) {
      fprintf(stderr, "Draining send/recv timeout, will perform checkout\n");
      break;
    }
    timeout_counter++;
  }
  removePendingSendRequests();
}

// FIXME: isBufferedPacket and consumeBufferedPacket both search
// in the g_message_queue with the same condition. Maybe we can
// combine them into one function.
bool
isBufferedPacket(int source, int tag, MPI_Comm comm, int *flag,
                 MPI_Status *status)
{
  bool ret = false;
  dmtcp::vector<mpi_message_t*>::iterator req =
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

int
consumeBufferedPacket(void *buf, int count, MPI_Datatype datatype,
                      int source, int tag, MPI_Comm comm,
                      MPI_Status *mpi_status, int size)
{
  int cpysize;
  mpi_message_t *foundMsg = NULL;
  dmtcp::vector<mpi_message_t*>::iterator req =
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
  memset(g_sendBytesByRank, 0, g_world_size * sizeof(int));
  memset(g_bytesSentToUsByRank, 0, g_world_size * sizeof(int));
  memset(g_recvBytesByRank, 0, g_world_size * sizeof(int));
}
