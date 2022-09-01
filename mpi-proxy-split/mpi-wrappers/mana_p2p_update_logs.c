#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
#include <string.h>
// To support MANA_P2P_LOG and MANA_P2P_REPLAY:
#define USE_READALL
#define USE_WRITEALL
#include "p2p-deterministic.h"

void fill_in_log(struct p2p_log_msg *p2p_log);
int show_log(char *name);

int main(int argc, char *argv[]) {
  char buf[100];
  int rank;
  int rc;

  MPI_Init(NULL, NULL);
  if (argc == 2) {
    rc = show_log(argv[1]);
    return rc;
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  snprintf(buf, sizeof(buf)-1, P2P_LOG_MSG, rank);
  int fd_log = open(buf, O_RDWR);
  if (fd_log == -1) {
    perror("get_next_msg: open");
    fprintf(stderr, "rank: %d\n", rank); fflush(stdout);
    exit(1);
  }

  while (1) {
    struct p2p_log_msg *p2p_log = NULL;
    off_t offset = lseek(fd_log, 0, SEEK_CUR);
    int p2p_log_size;
    rc = readp2pmsg(fd_log, &p2p_log, &p2p_log_size);
    if (rc == 0) {
      free(p2p_log);
      break;
    }
    if (p2p_log->request != MPI_REQUEST_NULL) {
      fill_in_log(p2p_log);
    }
    lseek(fd_log, offset, SEEK_SET);
    writeall(fd_log, p2p_log, p2p_log_size);
    free(p2p_log);
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

  static off_t req_start_offset = 0;
  struct p2p_log_request p2p_request;
  int fd2 = dup(fd_request);
  if (req_start_offset != 0) {
    lseek(fd2, req_start_offset, SEEK_SET);
    req_start_offset = 0;
  }
  while (1) {
    int rc = readall(fd2, &p2p_request, sizeof(p2p_request));
    if (rc == 0) return;
    if (p2p_request.request == p2p_log->request) {
      p2p_log->source = p2p_request.source;
      p2p_log->tag = p2p_request.tag;
      break;
    }
    // The request sequence is out of order. This could happen when MPI_Wait
    // for a high request number is called first followed by MPI_Wait for a
    // lower request number.
    if (req_start_offset == 0 && p2p_request.request > p2p_log->request) {
      req_start_offset = lseek(fd2, 0, SEEK_CUR) - sizeof(p2p_request);
    }
  }
  close(fd2);
}

int show_msg_file(char *name)
{
  int fd_log = open(name, O_RDWR);
  if (fd_log == -1) {
    fprintf(stderr, "show_log_file open: fle %s, error: %s\n", name, strerror(errno));
    return 1; 
  }

  while (1) {
    struct p2p_log_msg *p2p_log = NULL;
    int p2p_log_size;
    int rc;
    rc = readp2pmsg(fd_log, &p2p_log, &p2p_log_size);
    if (rc == 0) {
      free(p2p_log);
      break;
    }

    /* Checksum */
    char lrc = checksum_lrc(p2p_log->data, p2p_log_size);

    printf("MSG(request,source,tag,count,comm,size,lrc): %d,%d,%d,%d,%d,%d,%x\n",
	   p2p_log->request, p2p_log->source, p2p_log->tag, p2p_log->count,
	   p2p_log->comm, p2p_log_size, lrc & 0xff);
    free(p2p_log);
  }
  return 0;
}

int show_request_file(char *name)
{
  int fd_log = open(name, O_RDWR);
  if (fd_log == -1) {
    fprintf(stderr, "show_request_file open: fle %s, error: %s\n", name, strerror(errno));
    return 1;
  }

  while (1) {
    struct p2p_log_request p2p_request;
    int rc = readall(fd_log, &p2p_request, sizeof(p2p_request));
    if (rc == 0) {
      break;
    }

    printf("MSG(request,source,tag): %d,%d,%d\n",
	   p2p_request.request, p2p_request.source, p2p_request.tag);
  }
  return 0;
}
 
int show_log(char *name)
{
  int rank;

  if (sscanf(name, P2P_LOG_MSG, &rank) == 1) {
    show_msg_file(name);
  } else if (sscanf(name, P2P_LOG_REQUEST, &rank) == 1) {
    show_request_file(name);
  } else {
    fprintf(stderr, "Not a p2p log fle %s, error: %s\n", name, strerror(errno));
    return 1;
  }

  return 0;
}
