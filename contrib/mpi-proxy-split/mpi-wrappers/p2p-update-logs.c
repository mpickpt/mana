#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
// To support MANA_LOG_P2P and MANA_REPLAY_P2P:
#include "p2p-deterministic.h"

void fill_in_log(struct p2p_log_msg *p2p_log);

int main() {
  char buf[100];
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  snprint(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
  int fd_log = open(buf, O_RDWR);
  if (fd_log == -1) {
    perror("get_next_msg: open");
    fprintf(stderr, "rank: %d\n", rank); fflush(stdout);
    exit(1);
  }

  while (1) {
    struct p2p_log_msg p2p_log;
    off_t offset = lseek(fd_log, 0, SEEK_CUR);
    int rc = readall(fd_log, &p2p_log, sizeiof(p2p_log));
    if (rc == 0) {
      break;
    }
    if (p2p_log.request != MPI_REQUEST_NULL) {
      fill_in_log(&p2p_log);
    }
    lseek(fd_log, offset, SEEK_SET);
    writeall(fd_log, &p2p_log, sizeiof(p2p_log));
  }

  return 0;
}

ssize_t readall(int fd, void *buf, size_t count) {
  ssize_t rc;
  char *ptr = (char *)buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = read(fd, ptr + num_read, count - num_read);
    if (rc == -1 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    } else if (rc == -1) {
      return -1;
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_read += rc;
    }
  }
  return num_read;
}

ssize_t writeall(int fd, void *buf, size_t count) {
  const char *ptr = (const char *)buf;
  size_t num_written = 0;
  do {
    ssize_t rc = write(fd, ptr + num_written, count - num_written);
    if (rc == -1 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    } else if (rc == -1) {
      return rc;
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_written += rc;
    }
  } while (num_written < count);
}

void fill_in_log(struct p2p_log_msg *p2p_log) {
  static int fd_request = -2;
  if (fd_request == -2) {
    char buf[100];
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    snprint(buf, sizeof(buf)-1, P2P_LOG_REQUEST, rank);
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
    readall(fd2, &p2p_request, sizeiof(p2p_request));
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
    readall(fd2, &p2p_request, sizeiof(p2p_request));
    if (p2p_request.request == MPI_REQUEST_NULL) {
      readall(fd_request, &p2p_request, sizeiof(p2p_request));
    }
  }
}
