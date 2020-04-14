#ifndef _DRAIN_SEND_RECV_PACKETS_H
#define _DRAIN_SEND_RECV_PACKETS_H

#include <mpi.h>

#define DRAINED_REQUEST_VALUE 0xFFFFFFFF
#define MPI_SEND_RECV_DB  "SR_DB"
#define MPI_WRAPPER_DB    "WR_DB"
#define MPI_US_DB         "US_DB"

typedef struct __wr_counts
{
  int sendCount;
  int isendCount;
  int recvCount;
  int irecvCount;
  int sendrecvCount;
} wr_counts_t;

extern wr_counts_t g_counts;

typedef enum __mpi_req
{
  ISEND_REQUEST,
  IRECV_REQUEST,
  DRAINED_EARLY,
} mpi_req_t;

typedef struct __mpi_message
{
  void *buf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
} mpi_message_t;

typedef struct __mpi_call_params
{
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  int remote_node;  // dest or source
  int tag;
} mpi_call_params_t;

typedef struct __mpi_async_call
{
  // control data
  bool serviced;
  mpi_req_t type;
  // request parameters
  const void *sendbuf;
  void *recvbuf;
  mpi_call_params_t params;
  // async parameters
  MPI_Status status;
  MPI_Request *request;
  MPI_Request req; // Original request value
  int flag;
} mpi_async_call_t;

typedef struct __send_recv_totals
{
  int rank;
  uint64_t sends;
  uint64_t recvs;
  int countSends;
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

extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request* );
extern void clearPendingRequestFromLog(MPI_Request* , MPI_Request );
extern bool isBufferedPacket(int , int , MPI_Comm , int* , MPI_Status* , int *);
extern bool isServicedRequest(MPI_Request* , int* , MPI_Status* );
extern int consumeBufferedPacket(void* , int , MPI_Datatype , int ,
                                 int , MPI_Comm , MPI_Status* , int );

#endif // ifndef _DRAIN_SEND_RECV_PACKETS_H
