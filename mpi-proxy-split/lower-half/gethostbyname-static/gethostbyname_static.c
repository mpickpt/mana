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

// TESTING:  make
// USAGE:  Compile gethostbyname*.{c,h} into your statically linked application
//         It will now call the gethostbyname_r, getaddrinfo defined here
//         instead of the versions in glibc.  The versions in glibc would
//         simply report that those functions are not supported statically.

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netdb.h>
#include "gethostbyname_static.h"
#include "gethostbyname_utils.ic"

struct hostent_result hostent_result;
struct addrinfo_result addrinfo_result;

void patch_pointers_of_hostent_result(struct hostent_result* hostent_result) {
  long diff = (char *)&(hostent_result->here) - hostent_result->here;
  hostent_result->hostent.h_name += diff;
  void *rhs1 = (char *)hostent_result->hostent.h_aliases + diff;
  hostent_result->hostent.h_aliases = rhs1;
  void *rhs2 = (char *)hostent_result->hostent.h_addr_list + diff;
  hostent_result->hostent.h_addr_list = rhs2;
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

void patch_pointers_of_addrinfo_result(struct addrinfo_result* addrinfo_result)
{
  long diff = (char *)&(addrinfo_result->here) - addrinfo_result->here;
  struct addrinfo *addrinfo = (struct addrinfo *)(addrinfo_result->padding);
  while (addrinfo != NULL) {
    if (addrinfo->ai_canonname != NULL) {
      addrinfo->ai_canonname += diff;
    }
    void *rhs1 = (char *)addrinfo->ai_addr + diff;
    addrinfo->ai_addr = rhs1;
    if (addrinfo->ai_next != NULL) {
      void *rhs2 = (char *)addrinfo->ai_next + diff;
      addrinfo->ai_next = rhs2;
    }
    addrinfo = addrinfo->ai_next;
  }
}

static int error(struct hostent **result) {
  if (result != NULL) {
    *result = NULL; // man page says to set result to NULL;  Probably a bug.
  }
  return -1;
}


// For getaddrinfo or gethostbyname_r
void execvpProxyWithArgv(const char *file, const char *arg1, const char *arg2) {
    char *argv[] = {(char *)file, (char *)arg1, (char *)arg2, NULL};
    int rc = execvp("gethostbyname_proxy", argv);
    // NOTREACHED unless execvp() fails.  Try to execvp alternate path to file.
    // Next, look for gethostbyname_proxy in same dir as this executable.
    char buf[10000];
    int num = readlink("/proc/self/exe", buf, sizeof(buf));
    if (num == -1) {perror("readlink"); exit(1);}
    // Verify neither 'buf' nor 'dirname(buf) + "/gethostbyname_proxy"' overflow
    assert(num + 1 + sizeof("/gethostbyname_proxy") < sizeof(buf));
    buf[num] = '\0';
    char *dir = dirname(buf);
    assert(strlen(dir) + sizeof("gethostbyname_proxy") < sizeof(buf));
    char *path = strcat(dir, "/gethostbyname_proxy");
    rc = execvp(path, argv);
    // This is a real error.
    snprintf(buf, sizeof(buf), "execvp(gethostbyname_proxy) for %s", file);
    perror(buf);
    exit(1);
}

// FIXME:  The GLIBC man page is ambiguous.  Does '*result' need to
//         point to a static 'struct hostent' buffer?
//         If so, we could create a 'struct hostent' in 'padding'.
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
    execvpProxyWithArgv("gethostbyname_r", name, NULL);
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
    else if (hostent_result.result == NULL) {
      result = NULL;
      h_errno = hostent_result.h_errno_value;
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

struct hostent *gethostbyname(const char *name) {
  int h_errno_local;
  struct hostent ret;
  char buf[10000];
  size_t buflen = sizeof(buf);
  struct hostent *result;
  int rc = gethostbyname_r(name, &ret, buf, buflen, &result, &h_errno_local);
  if (rc != 0) {
    h_errno = h_errno_local;
    return NULL;
  } else {
    return result;
  }
}

int getaddrinfo(const char *__restrict node,
                const char *__restrict service,
                const struct addrinfo *__restrict hints,
                struct addrinfo **__restrict res) {
  int pipefdin[2];
  int pipefdout[2];
  pipe(pipefdin);
  pipe(pipefdout);
  int childpid = fork();
  if (childpid == 0) {
    dup2(pipefdin[0], 0);
    close(pipefdin[0]);
    dup2(pipefdout[1], 1);
    close(pipefdout[1]);
    execvpProxyWithArgv("getaddrinfo", node, service);
  } else if (childpid > 0) {
    if (hints != NULL) {
      writeall(pipefdin[1], hints, sizeof(*hints));
      // hints->ai_addr == NULL on input; no need to send *(hints->ai_addr)
    } else {
      struct sockaddr tmp;
      *(int *)&tmp = -1;
      // *(int *)hints overlaps with hints->ai_flags.  But hints->ai_flags
      //   should never have all bits set (equivalent to -1).
      writeall(pipefdin[1], &tmp, sizeof(*hints));
    }
    close(pipefdin[1]);
    // addrinfo_result is of fixed size, defined in gethostbyname_static.h
    int rc = readall(pipefdout[0], &addrinfo_result, sizeof(addrinfo_result));
    assert(rc == sizeof(addrinfo_result));
    close(pipefdin[0]);
    int status;
    waitpid(childpid, &status, 0);
    *res = (struct addrinfo *)addrinfo_result.padding;
    patch_pointers_of_addrinfo_result(&addrinfo_result);
    return WEXITSTATUS(status);
  } else {
    perror("fork"); return 1;
  }
}

void freeaddrinfo(struct addrinfo *res) {
  return;
}

#ifdef STANDALONE
# undef BYTE
# define BYTE(x) *((unsigned char*)ret.h_addr_list[0]+(x))
# define BYTE2(x) *((unsigned char*)&(((struct sockaddr_in *)(res2->ai_addr))->sin_addr)+(x))
int main(int argc, char *argv[]) {
  struct hostent ret;
  char buf[10000];
  struct hostent *result;
  int h_errnop;
  if (argc == 2) {
    gethostbyname_r(argv[1], &ret, buf, sizeof(buf), &result, &h_errnop);
    printf("First and second alias: %s %s\n",
           ret.h_aliases[0], ret.h_aliases[1]); 
    printf("First address: %u.%u.%u.%u\n",
           BYTE(0), BYTE(1), BYTE(2), BYTE(3));
  } else if (argc == 3) {
    struct addrinfo *res;
    struct addrinfo hints;
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;

    int rc1 = getaddrinfo(argv[1], argv[2], NULL, &res);
    assert(rc1 == 0);
    int rc2 = getaddrinfo(argv[1], argv[2], &hints, &res);
    assert(rc2 == 0);
    struct addrinfo *res2 = res;
    while (res2 != NULL) {
      // See /usr/include/linux/in.h  for protocols
      printf("getaddrinfo: canonname %s, protocol: %u,"
             " sin_port %d, sin_addr %u.%u.%u.%u\n",
             res2->ai_canonname, res2->ai_protocol,
             ((struct sockaddr_in *)(res2->ai_addr))->sin_port,
             BYTE2(0), BYTE2(1), BYTE2(2), BYTE2(3));
      res2 = res2->ai_next;
    }
    freeaddrinfo(res);
  } else {
    printf("USAGE2: '%s <HOST>' OR\n '%s <HOST> <SE2RVICE2>'\n", argv[0], argv[0]);
    return 1;
  }
  return 0;
}
#endif
