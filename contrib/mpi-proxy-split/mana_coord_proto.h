#include "mpi.h"
#ifndef _MANA_COORD_PROTO_
#define _MANA_COORD_PROTO_

#define GID_LIST_SIZE 1024

typedef enum __phase_t
{
  ST_ERROR = -1,
  ST_UNKNOWN, /* State 0 shouldn't be confused with a state used in the algo. */
  HYBRID_PHASE1,
  IN_TRIVIAL_BARRIER,
  PHASE_1,
  IN_CS_NO_TRIV_BARRIER,
  IN_CS,
  FINISHED_PHASE2,
  FINISHED_PHASE2_NO_TRIV_BARRIER,
  IS_READY,
} phase_t;

typedef enum __query_t
{
  NONE = -1,
  Q_UNKNOWN, /* State 0 shouldn't be confused with a state used in the algo. */
  INTENT,
  SECOND_INTENT,
  FREE_PASS,
  CONTINUE,
  DO_TRIV_BARRIER,
} query_t;

// Struct to encapsulate the checkpointing state of a rank
typedef struct __rank_state_t
{
  int rank;       // MPI rank
  MPI_Comm comm;  // MPI communicator object
  phase_t st;     // Checkpointing state of the MPI rank
} rank_state_t;

typedef struct __mana_msg_t
{
  query_t msg;
  unsigned int gids_in_cs_no_triv[GID_LIST_SIZE];
} mana_msg_t;

#endif // ifndef _MANA_COORD_PROTO_
