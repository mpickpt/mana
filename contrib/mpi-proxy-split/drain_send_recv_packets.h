#ifndef _DRAIN_SEND_RECV_PACKETS_H
#define _DRAIN_SEND_RECV_PACKETS_H

#include <mpi.h>

#include "mana_coord_proto.h"

#define DRAINED_REQUEST_VALUE 0xFFFFFFFF

// Stores the counts of executed wrappers (send, isend, recv, irecv); useful
// while debugging
extern wr_counts_t g_counts;

typedef enum __mpi_req
{
  ISEND_REQUEST,
  IRECV_REQUEST,
  IBCAST_REQUEST,
  IBARRIER_REQUEST,
  IREDUCE_REQUEST,
  DRAINED_EARLY,
} mpi_req_t;

// Struct to store and return the MPI message (data) during draining and
// resuming
typedef struct __mpi_message
{
  void *buf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
} mpi_message_t;

// Struct to store the params of an async send/recv
typedef struct __mpi_call_params
{
  int count;        // Count of data items
  MPI_Datatype datatype; // Data type
  int size;         // Size of each data item
  MPI_Comm comm;    // MPI communicator
  int remote_node;  // Can be dest or source depending on the the call type
  int tag;          // MPI message tag
  MPI_Op op;        // Used in Ireduce
  int root;         // Used in Ireduce and Ibcast
} mpi_call_params_t;

// Struct to store the metadata of an async MPI send/recv call
typedef struct __mpi_async_call
{
  // control data
  bool serviced;   // True if the message was drained successfully
  mpi_req_t type;  // See enum __mpi_req
  // request parameters
  const void *sendbuf;
  void *recvbuf;
  mpi_call_params_t params; // Params of the MPI call
  // async parameters
  MPI_Status status;
  MPI_Request *request;
  MPI_Request req; // Original request value
  int flag;
} mpi_async_call_t;

// Increments the global number of receives by 1
extern void updateLocalRecvs(int count);

// Increments the global number of sends by 1
extern void updateLocalSends(int count);

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match the globally stored values in the plugin
extern void getLocalRankInfo();

// Publishes the count of total sent and received messages (along with the count
// unserviced Isends) to the DMTCP coordinator.
extern void registerLocalSendsAndRecvs();

// Drains unserviced MPI send packets to ensure there are no pending MPI
// messages on the channel
extern void drainMpiPackets();

// Sets the name of the checkpoint directory of the current process to
// "ckpt_rank_<RANK>", where RANK is the MPI rank of the process.
extern void updateCkptDirByRank();

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match with the pre-checkpoint values in the plugin.
extern void verifyLocalInfoOnRestart();

// Restores the state of MPI P2P communication by replaying any pending
// MPI_Isend and MPI_Irecv requests post restart
extern void replayMpiOnRestart();

// Resets the global counters to 0 and clears the global lists of saved MPI
// messages and metadata
extern void resetDrainCounters();

// Saves the async send/recv call of the given type and params to a global map
// indexed by the MPI_Request object pointer 'req'
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request* );
extern void addPendingIbarrierToLog(mpi_req_t req, MPI_Comm comm,
                                   MPI_Request* rq);
extern void addPendingIbcastToLog(mpi_req_t req, void* buffer, int cnt,
                                   MPI_Datatype type, int root,
                                   MPI_Comm comm, MPI_Request* rq);
extern void addPendingIreduceToLog(mpi_req_t req, const void* sbuf, void* rbuf,
                                   int cnt, MPI_Datatype type, MPI_Op op,
                                   int root, MPI_Comm comm, MPI_Request* rq);

// Removes the async send/recv call corresponding to the given MPI_Request
// object 'req' from the global map, 'g_async_calls'
extern void clearPendingRequestFromLog(MPI_Request* , MPI_Request );

// Returns true if the message was locally buffered during the draining phase
extern bool isBufferedPacket(int , int , MPI_Comm , int* , MPI_Status* , int *);

// Returns true if the message was serviced during the draining phase
// (NOTE: This should always be called with checkpointing disabled.)
extern bool isServicedRequest(MPI_Request* , int* , MPI_Status* );

// Returns the locally buffered message data in the output 'buf'
extern int consumeBufferedPacket(void* , int , MPI_Datatype , int ,
                                 int , MPI_Comm , MPI_Status* , int );

#endif // ifndef _DRAIN_SEND_RECV_PACKETS_H
