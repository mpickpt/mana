#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#define USE_READALL
#define USE_WRITEALL
#include "p2p-deterministic.h"

void fill_in_log(struct p2p_log_msg *p2p_log);

int main() {
  char buf[100];
  int rank;
  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
  int fd_log = open(buf, O_RDWR);
  if (fd_log == -1) {
    perror("get_next_msg: open");
    fprintf(stderr, "rank: %d\n", rank); fflush(stdout);
    exit(1);
  }

  while (1) {
    struct p2p_log_msg p2p_log;
    off_t offset = lseek(fd_log, 0, SEEK_CUR);
    int rc = readall(fd_log, &p2p_log, sizeof(p2p_log));
    if (rc == 0) {
      break;
    }
    if (p2p_log.request != MPI_REQUEST_NULL) {
      fill_in_log(&p2p_log);
    }
    lseek(fd_log, offset, SEEK_SET);
    writeall(fd_log, &p2p_log, sizeof(p2p_log));
  }
  MPI_Finalize();

  return 0;
}

void fill_in_log(struct p2p_log_msg *p2p_log) {
  static int fd_request = -2;
  if (fd_request == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprintf(buf, sizeof(buf)-1, P2P_LOG_REQUEST, rank);
    fd_request = open(buf, O_RDONLY);
    if (fd_request == -1) {
      perror("update_requests: get next msg: open");
      exit(1);
    }
  }

  static off_t request_start = 0;
  struct p2p_log_request p2p_request;
  int fd2 = dup(fd_request);
  while (1) {
    readall(fd2, &p2p_request, sizeof(p2p_request));
    if (p2p_request.request == p2p_log->request) {
      p2p_log->source = p2p_request.source;
      p2p_log->tag = p2p_request.tag;
      p2p_log->request = MPI_REQUEST_NULL;
      break;
    }
  }
  close(fd2);
  fd2 = dup(fd_request);
  while (1) {
    readall(fd2, &p2p_request, sizeof(p2p_request));
    if (p2p_request.request == MPI_REQUEST_NULL) {
      readall(fd_request, &p2p_request, sizeof(p2p_request));
    }
  }
}
