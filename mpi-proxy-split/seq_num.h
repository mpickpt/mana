#ifndef SEQ_NUM_H
#define SEQ_NUM_H

#include <mpi.h>
#include <pthread.h>
#include <unordered_map>

typedef enum _phase_t {
  IN_CS,
  IS_READY
} phase_t;

// Global communicator for MANA internal use
extern MPI_Comm g_world_comm;

// The main functions of the sequence number algorithm for MPI collectives
void commit_begin(MPI_Comm comm);
void commit_finish(MPI_Comm comm);

void drain_mpi_collective();
void share_seq_nums();
int check_seq_nums();
void print_seq_nums();
void seq_num_init();
void seq_num_reset();
void seq_num_destroy();

extern std::unordered_map<unsigned int, unsigned long> seq_num;
extern std::unordered_map<unsigned int, unsigned long> target;
extern std::unordered_map<MPI_Comm, unsigned int> ggid_table;
typedef std::pair<unsigned int, unsigned long> comm_seq_pair_t;

#endif // ifndef SEQ_NUM_H
