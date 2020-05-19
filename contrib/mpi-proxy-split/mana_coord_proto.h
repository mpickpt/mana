#ifndef _MANA_COORD_PROTO_
#define _MANA_COORD_PROTO_

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

typedef enum __phase_t
{
  UNKNOWN = -1,
  IS_READY,
  PHASE_1,
  IN_CS,
  OUT_CS,
  READY_FOR_CKPT,
  PHASE_2,
} phase_t;

typedef enum __query_t
{
  NONE = -1,
  INTENT,
  GET_STATUS,
  FREE_PASS,
  CKPT,
} query_t;

// Struct to encapsulate the checkpointing state of a rank
typedef struct __rank_state_t
{
  int rank;       // MPI rank
  MPI_Comm comm;  // MPI communicator object
  phase_t st;     // Checkpointing state of the MPI rank
} rank_state_t;

// Struct to store the number of times send, isend, recv, and irecv wrappers
// were executed
typedef struct __wr_counts
{
  int sendCount;     // Number of times MPI_Send wrapper was called
  int isendCount;    // Number of times MPI_Isend wrapper was called
  int recvCount;     // Number of times MPI_Recv wrapper was called
  int irecvCount;    // Number of times MPI_Irecv wrapper was called
  int sendrecvCount; // Number of times MPI_Sendrecv wrapper was called
} wr_counts_t;

// Struct to store the MPI send/recv counts of a rank
typedef struct __send_recv_totals
{
  int rank;         // MPI rank
  uint64_t sends;   // Number of completed sends
  uint64_t recvs;   // Number of completed receives
  int countSends;   // Number of unserviced sends
} send_recv_totals_t;

#endif // ifndef _MANA_COORD_PROTO_
