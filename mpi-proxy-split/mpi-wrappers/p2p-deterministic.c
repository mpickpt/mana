#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
//   Define USE_READALL/WRITEALL if using readall/writeall.
#define USE_READALL
#define USE_WRITEALL
#include "p2p-deterministic.h"

int p2p_deterministic_skip_save_request = 0;
/**********************************************************************************
 * USAGE (workflow):
 *   MANA_P2P_LOG=1 mana_launch -i SECONDS ... mpi_executable
 *   # Allow MANA to continue executing for a minute or two after checkpointing
 *   # MANA will then complete any pending requests, and use that information
 *   #   to replace MPI_ANY_TAG/SOURCE by the actual tag and source that were used.
 *   # This creates the files "p2p_log_%d.txt" and "p2p_log_request_%d.txt"
 *   # The file "p2p_log_%d.txt" logs each msg received by MPI_Irecv.
 *   # The file "p2p_log_request_%d.txt" logs the status of each request
 *   #   completed by MPI_Wait or MPI_Test.
 *   # FIXME:  Note that MPI_Waitsome/Waitany/Waitall are not handled yet.
 *   mpirun mana_p2p_update_logs
 *   MANA_P2P_REPLAY=1 mana_restart ... --restartdir ./DIR
 *   # OR:
 *   MANA_P2P_REPLAY=1 mana_launch ... mpi_executable
 **********************************************************************************/

/***********************************************************
 * Utilities for point-to-point deterministic log-and-replay
 ***********************************************************/

static struct p2p_log_msg next_msg_entry = {0, MPI_CHAR, 0, 0, 0, MPI_COMM_NULL, MPI_REQUEST_NULL};
static struct p2p_log_msg *next_msg = NULL;
static int logging_start = 0;

void p2p_log(int count, MPI_Datatype datatype, int source, int tag,
             MPI_Comm comm, MPI_Status *status, MPI_Request *request,
             void *buf) {
  if (status) {
    source = status->MPI_SOURCE;
    tag = status->MPI_TAG;
    int count;
    MPI_Get_count(status, MPI_CHAR, &count);
    datatype = MPI_CHAR;
  }
  set_next_msg(count, datatype, source, tag, comm, NULL, request, buf);
}

int iprobe_next_msg(struct p2p_log_msg *p2p_msg) {
  int rc = 0;
  if (!next_msg) {
    rc = get_next_msg_iprobe(NULL);
    if (rc == 1) return -1;
  }
  if (p2p_msg) {
    *p2p_msg = next_msg_entry;
  }
  return (next_msg_entry.comm != MPI_COMM_NULL);
}

int get_next_msg_irecv(struct p2p_log_msg *p2p_msg) {
  static int fd = -1;
  static int found_next_msg = 0;
  struct p2p_log_msg *msg = NULL;
  int msg_size = 0;
  int rc = 1;
  if (fd == -1) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
    fd = open(buf, O_RDONLY);
    if (fd == -1) {
      return 1;
    }
  }
  // Irecv request is _not_ MPI_REQUEST_NULL.
  // Read current Irecv entries
  do {
    rc = readp2pmsg(fd, &msg, &msg_size);
    if (rc <= 0) {
      found_next_msg = 0;
      return 1;
    }
    next_msg_entry = *msg;
    free(msg);
  } while (next_msg_entry.request == MPI_REQUEST_NULL);
  if (found_next_msg == 0) {
    // Read next Irecv entriea
    do {
      rc = readp2pmsg(fd, &msg, &msg_size);
      if (rc <= 0) return 1;
      next_msg_entry = *msg;
      free(msg);
    } while (next_msg_entry.request == MPI_REQUEST_NULL);
  }
  // Found the next message
  found_next_msg = 1;
  if (p2p_msg != NULL) *p2p_msg = next_msg_entry;
  return 0;
}

int get_next_msg_iprobe(struct p2p_log_msg *p2p_msg) {
#define UNINITIALIZED -2
  static int fd = UNINITIALIZED;
  int rc = 1;
  if (fd == UNINITIALIZED) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
    fd = open(buf, O_RDONLY);
    if (fd == -1) {
      fd = UNINITIALIZED;
      // The log file is created by set_next_msg() which
      // is called later. It is expected that first open
      // fails. Return 1 here to indicate that there is
      // not any next message yet.
      return 1;
    }
  }
  // Read Iprobe entries
  do {
    rc = readall(fd, &next_msg_entry, sizeof(next_msg_entry));
    if (rc <= 0) return 1;
  } while (next_msg_entry.request != MPI_REQUEST_NULL);
  do {
    rc = readall(fd, &next_msg_entry, sizeof(next_msg_entry));
    if (rc <= 0) return 1;
  } while (next_msg_entry.request != MPI_REQUEST_NULL);
  // Found the next message
  if (p2p_msg != NULL) *p2p_msg = next_msg_entry;
  if (next_msg == NULL) next_msg = &next_msg_entry;
  return 0;
}
// comm defined
// Either status and request are non-null, or source, tag defined.
void set_next_msg(int count, MPI_Datatype datatype,
                  int source, int tag, MPI_Comm comm,
                  MPI_Status *status, MPI_Request *request,
                  void *data) {
  struct p2p_log_msg *p2p_msg;
  static int fd = -2;
  if (fd == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
    fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
      perror("set_next_msg: open: couldn't create log file");
      exit(1);
    }
  }
  logging_start = 1;
  int size;
  MPI_Type_size(datatype, &size);
  p2p_msg = malloc(sizeof(*p2p_msg) + size * count);
  p2p_msg->count = count;
  p2p_msg->datatype = datatype;
  p2p_msg->source = source;
  p2p_msg->tag = tag;
  p2p_msg->comm = comm;
  memcpy(p2p_msg->data, data, size * count);
  // request is NULL for MPI_Recv (no such arg), and for MPI_Wait (block until done)
  // We save requests in a separate file.
  p2p_msg->request = (request ? *request : MPI_REQUEST_NULL);
  if (status != NULL) {
    p2p_msg->source = status->MPI_SOURCE;
    p2p_msg->tag = status->MPI_TAG;
    if (status->MPI_ERROR) {
      fprintf(stderr, "Recv with error:  %d\n", status->MPI_ERROR);
      free(p2p_msg);
      exit(1);
    }
  }
  writeall(fd, p2p_msg, sizeof(*p2p_msg) + size * count);
  static int i = 100;
  if (i-- == 0) {
    fflush(stdout);
    i = 100;
  }
  free(p2p_msg);
}

void p2p_replay_pre_irecv(int count, MPI_Datatype datatype, int *source,
                          int *tag, MPI_Comm comm) {
  struct p2p_log_msg p2p_msg;
  int rc = 0;
  rc = get_next_msg_irecv(&p2p_msg);
  if (rc == 1) return; // no next msg
  assert(*source == MPI_ANY_SOURCE || *source == p2p_msg.source);
  assert(*tag == MPI_ANY_TAG || *tag == p2p_msg.tag);
  *source = p2p_msg.source;
  *tag = p2p_msg.tag;
}

void p2p_replay_post_iprobe(int *source, int *tag, MPI_Comm comm,
                            MPI_Status *status, int *flag)
{
  static int MPI_Iprobe_recurse = 0;
  if (!MPI_Iprobe_recurse) {
    struct p2p_log_msg p2p_msg;
    int rc = 0;
    rc = iprobe_next_msg(NULL);
    if (rc == -1) return; // no next message
    if (*flag != 0 || iprobe_next_msg(NULL) == 1) { // There is a message.
      while (iprobe_next_msg(NULL) == 0) {
        // consume messages where log says MPI_Iprobe: flag==0
        rc = get_next_msg_iprobe(&p2p_msg);
	if (rc == 1) return;
      } // Now log says MPI_Iprobe returned with flag==1
      // Call blocking probe until implicit *flag and log agree
      assert(*source == MPI_ANY_SOURCE || *source == p2p_msg.source);
      assert(*tag == MPI_ANY_TAG || *tag == p2p_msg.tag);
      MPI_Iprobe_recurse = 1;
      MPI_Probe(p2p_msg.source, p2p_msg.tag, p2p_msg.comm, status);
      MPI_Iprobe_recurse = 0;
      *flag = 1;
    } else { // else: *flag == 0; log says MPI_Iprobe returned with flag==0
      get_next_msg_iprobe(&p2p_msg); // flag and log agree: consume this message
      *flag = 0;
    }
  }
}


/* source and tag are INOUT parameters */
void  p2p_replay(int count, MPI_Datatype datatype, int *source, int *tag,
                 MPI_Comm comm) {
  // Fix me: need to differentiate the caller is Recv or Probe
  assert(0);
#if 0
  struct p2p_log_msg p2p_msg;
  get_next_msg(&p2p_msg);
  *source = p2p_msg.source;
  *tag = p2p_msg.tag;
  assert(comm = p2p_msg.comm);
#endif
}

/******************************
 * Utilities for requests
 ******************************/

void save_request_info(MPI_Request *request, MPI_Status *status) {
  struct p2p_log_request p2p_request;
  static int fd = -2;
  if (!logging_start) return;
  if (fd == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_REQUEST, rank);
    fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
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
