/* Copyright 2021 Gene Cooperman (gene@ccs.neu.edu)
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

// This must be identical to struct in gethostbyname_r_aux.c
struct hostent_result {
  char *here;  // The address of this field this was located in child process
  struct hostent hostent;
  char padding[10000];
  char *end;
} hostent_result;

void patch_pointers_of_hostent_result(struct hostent_result* hostent_result) {
  long diff = (char *)&(hostent_result->here) - hostent_result->here;
  hostent_result->hostent.h_name += diff;
  char *rhs1 = (char *)hostent_result->hostent.h_aliases + diff;
  hostent_result->hostent.h_aliases = (char **)rhs1;
  char *rhs2 = (char *)hostent_result->hostent.h_addr_list + diff;
  hostent_result->hostent.h_addr_list = (char **)rhs2;
  char **tmp = hostent_result->hostent.h_aliases;
  while (*tmp != NULL) {
    *tmp += diff;
    tmp++;
  }
  tmp = hostent_result->hostent.h_addr_list;
  while (*tmp != NULL) {
    *tmp += diff;
    tmp++;
  }
}

static int gethostbyname_errno;

static int error(struct hostent **result) {
  if (result != NULL) {
    *result = NULL; // man page says to set result to NULL;  Probably a bug.
  }
  return -1;
}

int readall(int fd, void *buf, size_t count) {
  char *buf2 = buf;
  while (count > 0) {
    int rc = read(fd, buf2, count);
    if (rc == -1 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    if (rc <= 0) {
      return -1; // zero means early EOF; -1 means error
    }
    buf2 += rc;
    count -= rc;
  }
  return buf2 - (char *)buf;
}

char *getProxyPath() {
  char buf[10000];
  readlink("/proc/self/exe", buf, sizeof(buf));
  char *dir = dirname(buf);
  assert(strlen(dir) + sizeof("gethostbyname_r_aux") < sizeof(buf));
  return strcpy(dir, "gethostbyname_r_aux");
}

// Call child process with 'name' argument.
// Child will fill in 'struct hostent_result' by calling gethostbyname()
//   and writing the 'hostent_result back' into the pipe.
int gethostbyname_r(const char *name,
    struct hostent *ret, char *buf, size_t buflen,
    struct hostent **result, int *h_errnop) {
  int pipefd[2];
  pipe(pipefd);
  int childpid = fork();
  if (childpid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], 1);
    close(pipefd[1]);
    setenv("PATH", ".", 1);
    char *argv[] = {NULL, NULL, NULL};
    argv[0] = getProxyPath();
    argv[1] = (char *)name;
    int rc = execvp(argv[0], argv);
    if (rc == -1) { perror("execvp"); }
  } else if (childpid > 0) {
    close(pipefd[1]);
    int rc = readall(pipefd[0], &hostent_result, sizeof(hostent_result));
    assert(rc == sizeof(hostent_result));
    int status;
    waitpid(childpid, &status, 0);
    if (WEXITSTATUS(status) == ERANGE) {
      errno = ERANGE;
      return -1;
    }
    else if (hostent_result.hostent.h_name == NULL) {
      result = NULL;
      return -1;
    } else {
      patch_pointers_of_hostent_result(&hostent_result);
      memcpy(ret, &hostent_result.hostent, sizeof(hostent_result.hostent));
      *result = ret;
      return 0;
    }
  } else {
    perror("fork"); error(result); return 1;
  }
}

// GNU extension.  We ignore af and pass this to gethostbyname().
// FIXME:  We should implement gethostbyname2_r and getaddrinfo
//         in the child proxy process.
int gethostbyname2_r(const char *name, int af,
    struct hostent *ret, char *buf, size_t buflen,
    struct hostent **result, int *h_errnop) {
  return gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
}

#ifdef STANDALONE
int main() {
  struct hostent ret;
  char buf[10000];
  struct hostent *result;
  int h_errnop;
  gethostbyname_r("localhost", &ret, buf, sizeof(buf), &result, &h_errnop);
  return 0;
}
#endif
