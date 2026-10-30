#ifndef SEQ_NUM_H
#define SEQ_NUM_H

#include <mpi.h>
#include <pthread.h>
#include <stdint.h>
#include <unordered_map>

typedef enum _phase_t {
  IN_CS,
  IS_READY
} phase_t;

// Global communicator for MANA internal use
extern MPI_Comm g_world_comm;

// The main functions of the sequence number algorithm for MPI collectives
void commit_begin(MPI_Comm comm);
// Enters a critical section like commit_begin(), but counts no call.
void commit_begin_uncounted();
void commit_finish(MPI_Comm comm);

void drain_mpi_collective();
void ckpt_kvdb_name(char *buf, size_t size, const char *what,
                    int attempt = -1);
void complete_pending_nonblocking_collectives();
int test_pending_nonblocking_collectives();
void share_seq_nums();
int check_seq_nums();
void print_seq_nums();
void seq_num_init();
void seq_num_reset();
void seq_num_destroy();

// The Collective Clock of a communicator on this process.  'seq', the sequence
// number, counts the collective calls that this process made on the
// communicator.  'target' is the number that 'seq' must reach before a
// checkpoint.
struct comm_clock {
  unsigned long seq;
  unsigned long target;
};

// Adds the clock of a new communicator, with 'seq' and 'target' at 0, and
// returns it.
comm_clock* add_comm_clock(uint64_t gid);

#endif // ifndef SEQ_NUM_H
