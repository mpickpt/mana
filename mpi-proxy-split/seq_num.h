#ifndef SEQ_NUM_H
#define SEQ_NUM_H

#include <mpi.h>
#include <pthread.h>
#include <functional>

typedef enum _reset_type_t {
  RESUME,
  RESTART
} reset_type_t;

typedef enum _seq_num_msg_t {
  UPDATE_TARGET,
  CONVERGED,
  CANCEL_CONVERGED
} seq_num_msg_t;

typedef enum _phase_t {
  IN_TRIVIAL_BARRIER,
  STOP_BEFORE_CS,
  IN_CS,
  IS_READY
} phase_t;

typedef enum __query_t
{
  NONE = -1,
  Q_UNKNOWN, /* State 0 shouldn't be confused with a state used in the algo. */
  Q_FAILED,
  INTENT,
  FREE_PASS,
  WAIT_STRAGGLER
} query_t;

// Struct to encapsulate the checkpointing state of a rank
typedef struct __rank_state_t
{
  int rank;       // MPI rank
  unsigned int comm;  // MPI communicator object
  phase_t st;     // Checkpointing state of the MPI rank
} rank_state_t;

// Global communicator for MANA internal use
extern MPI_Comm g_world_comm;

extern std::map<unsigned int, unsigned long> seq_num;
extern std::map<unsigned int, unsigned long> target;

// The main functions of the sequence number algorithm for MPI collectives
void commit_begin(MPI_Comm comm, bool passthrough);
void commit_finish(MPI_Comm comm, bool passthrough);

int twoPhaseCommit(MPI_Comm comm, std::function<int(void)>doRealCollectiveComm);
void drain_mpi_collective();
void share_seq_nums(std::map<unsigned int, unsigned long> &target);
int check_seq_nums(bool exclusive);
int print_seq_nums();
void seq_num_init();
void seq_num_destroy();
void seq_num_reset(reset_type_t reset_type);

#endif // ifndef SEQ_NUM_H
