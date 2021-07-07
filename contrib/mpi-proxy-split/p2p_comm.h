#ifndef _P2P_COMM_H
#define _P2P_COMM_H

#include <mpi.h>

typedef enum __mpi_req
{
  ISEND_REQUEST,
  IRECV_REQUEST,
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

// Struct to store the metadata of an async MPI send/recv call
typedef struct __mpi_async_call
{
  // control data
  mpi_req_t type;  // See enum __mpi_req
  // request parameters
  const void *sendbuf;
  void *recvbuf;
  int count;        // Count of data items
  MPI_Datatype datatype; // Data type
  MPI_Comm comm;    // MPI communicator
  int remote_node;  // Can be dest or source depending on the the call type
  int tag;          // MPI message tag
} mpi_async_call_t;

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match the globally stored values in the plugin
extern void getLocalRankInfo();

// Sets the name of the checkpoint directory of the current process to
// "ckpt_rank_<RANK>", where RANK is the MPI rank of the process.
extern void updateCkptDirByRank();

// Restores the state of MPI P2P communication by replaying any pending
// MPI_Isend and MPI_Irecv requests post restart
extern void replayMpiP2pOnRestart();

// Saves the async send/recv call of the given type and params to a global map
// indexed by the MPI_Request 'req'
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request);

// remove finished send/recv call from the global map
extern void clearPendingRequestFromLog(MPI_Request req);

// Publishes the unserviced sends' metadata to the DMTCP coordinator.
extern void registerUnservicedSends();

// Publishes the count of drained sends to the DMTCP coordinator.
extern void registerDrainedSends();

// Drains unserviced MPI send packets to ensure there are no pending MPI
// messages on the channel
extern void drainMpiPackets();

// Returns the locally buffered message data in the output 'buf'
extern int consumeBufferedPacket(void* , int , MPI_Datatype , int ,
                                 int , MPI_Comm , MPI_Status* , int );
#endif // ifndef _P2P_COMM_H
