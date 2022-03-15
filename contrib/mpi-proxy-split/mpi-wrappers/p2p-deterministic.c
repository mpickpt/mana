#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
// To support MANA_LOG_P2P and MANA_REPLAY_P2P:
#include "p2p-deterministic.h"

/***********************************************************
 * Utitilies for point-to-point deterministic log-and-replay
 ***********************************************************/

static struct p2p_log_msg next_msg_entry;
static struct p2p_log_msg *next_msg = NULL;

void p2p_log(int count, MPI_Datatype datatype, int source, int tag,
             MPI_Comm comm, MPI_Status *status, MPI_Request *request) {
  if (status) {
    source = status->MPI_SOURCE;
    tag = status->MPI_TAG;
    int count;
    MPI_Get_count(status, MPI_CHAR, &count);
    datatype = MPI_CHAR;
  }
  set_next_msg(count, datatype, source, tag, comm, NULL, request);
}

void initialize_next_msg(int fd) {
  next_msg = &next_msg_entry;
  readall(fd, next_msg, sizeof(*next_msg));
}

int iprobe_next_msg(struct p2p_log_msg *p2p_msg) {
  /* FIXME:  This isn't comiling yet.
  if (!next_msg) {
    initialize_next_msg(fd);
  }
  */
  if (p2p_msg) {
    *p2p_msg = next_msg_entry;
  }
  return (next_msg_entry.comm != MPI_COMM_NULL);
}

void get_next_msg(struct p2p_log_msg *p2p_msg) {
  static int fd = -2;
  if (fd == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
    fd = open(buf, O_RDONLY);
    if (fd == -1) {
      perror("get_next_msg: open");
      exit(1);
    }
  }
  if (!next_msg) {
    initialize_next_msg(fd);
  }
  *p2p_msg = next_msg_entry;
  readall(fd, next_msg, sizeof(*next_msg));
}


// comm defined
// Either status and request are non-null, or source, tag defined.
void set_next_msg(int count, MPI_Datatype datatype,
                  int source, int tag, MPI_Comm comm,
                  MPI_Status *status, MPI_Request *request) {
  struct p2p_log_msg p2p_msg;
  static int fd = -2;
  if (fd == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
    fd = open(buf, sizeof(buf)-1,
              O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
    if (fd == -1) {
      perror("set_next_msg: open: couldn't create log file");
      exit(1);
    }
  }
  p2p_msg.count = count;
  p2p_msg.datatype = datatype;
  p2p_msg.source = source;
  p2p_msg.tag = tag;
  p2p_msg.comm = comm;
  p2p_msg.request = (request ? *request : MPI_REQUEST_NULL);
  // request is NULL for MPI_Recv, and for MPI_Wait
  // We save requests in a separate file.
  if (request != NULL) {
    p2p_msg.source = status->MPI_SOURCE;
    p2p_msg.tag = status->MPI_TAG;
    if (status->MPI_ERROR) {
      fprintf(stderr, "Recv with error:  %d\n", status->MPI_ERROR);
      exit(1);
    }
  }
  writeall(fd, &p2p_msg, sizeof(p2p_msg));
  static int i = 100; 
  if (i-- == 0) {
    fflush(stdout);
    i = 100;
  }
}

/* source and tag are INOUT parameters */
void  p2p_replay(int count, MPI_Datatype datatype, int *source, int *tag,
                 MPI_Comm comm) {
  struct p2p_log_msg p2p_msg;
  get_next_msg(&p2p_msg);
  *source = p2p_msg.source;
  *tag = p2p_msg.tag;
  assert(comm = p2p_msg.comm);
}

/******************************
 * Utilities for requests
 ******************************/

void save_request_info(MPI_Request *request, MPI_Status *status) {
  struct p2p_log_request p2p_request;
  static int fd = -2;
  if (fd == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_REQUEST, rank);
    fd = open(buf, sizeof(buf)-1,
              O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
    if (fd == -1) {
      perror("save_request_info: open: couldn't create request file");
      exit(1);
    }
  }
  p2p_request.source = status->MPI_SOURCE;
  p2p_request.tag = status->MPI_TAG;
  p2p_request.request = *request;
  writeall(fd, &p2p_request, sizeof(p2p_request));
  static int i = 10; 
  if (i-- == 0) {
    fflush(stdout);
    i = 10;
  }
}
