#ifndef _DRAIN_SEND_RECV_PACKETS_H
#define _DRAIN_SEND_RECV_PACKETS_H

#include <mpi.h>

#define DRAINED_REQUEST_VALUE 0xFFFFFFFF

// Key-value database containing the counts of sends, receives, and unserviced
// sends for each rank
// Mapping is (rank -> send_recv_totals_t)
#define MPI_SEND_RECV_DB  "SR_DB"

// Key-value database containing the metadata of unserviced sends for each rank
// Mapping is (rank -> mpi_call_params_t)
#define MPI_US_DB         "US_DB"

// Database containing the counts of wrappers (send, isend, recv, irecv)
// executed for each rank (useful while debugging)
// Mapping is (rank -> wr_counts_t)
#define MPI_WRAPPER_DB    "WR_DB"

// Struct to store the number of times send, isend, recv, and irecv wrappers
// were executed
typedef struct __wr_counts
{
  int sendCount;
  int isendCount;
  int recvCount;
  int irecvCount;
  int sendrecvCount;
} wr_counts_t;

// Stores the counts of executed wrappers (send, isend, recv, irecv); useful
// while debugging
extern wr_counts_t g_counts;

typedef enum __mpi_req
{
  ISEND_REQUEST,
  IRECV_REQUEST,
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
} mpi_call_params_t;

// Struct to store the metadata of an async MPI send/recv call
typedef struct __mpi_async_call
{
  // control data
  bool serviced;   // True if the message was drained successfully
  mpi_req_t type;  // ISEND_REQUEST or IRECV_REQUEST
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

// Struct to store the MPI send/recv counts of a rank
typedef struct __send_recv_totals
{
  int rank;         // MPI rank
  uint64_t sends;   // Number of completed sends
  uint64_t recvs;   // Number of completed receives
  int countSends;   // Number of unserviced sends
} send_recv_totals_t;

// Increments the global number of receives by 1
extern void updateLocalRecvs();

// Increments the global number of sends by 1
extern void updateLocalSends();

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

extern void resetDrainCounters();

// Saves the async send/recv call of the given type and params to a global map
// indexed by the MPI_Request object pointer 'req'
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request* );

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
