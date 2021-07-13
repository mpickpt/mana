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

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct hostent_result {
  char *here;  // The address of this field this was located in child process
  struct hostent hostent;
  char padding[10000];
  char *end;
} hostent_result;

int writeall(int fd, void *buf, size_t count) {
  char *buf2 = buf;
  while (count > 0) {
    int rc = write(fd, buf2, count);
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

// char **list is similar to 'char argv[]' in main()
// dest_p is INOUT param.
// IN: *dest_pt is the destination starting address.
// OUT: Set *dest_p to the next address after the final address when done.
// h_length is length of each item in list, or if it's 0, then item is a string.
void copy_pointer_list(char **dest_p, char **list, int h_length) {
  char *dest = *dest_p;
  char **list_start = list;
  char **dest_ptr = (char **)dest;
  while (*list != NULL) {
    *(char **)dest = *list;
    dest += sizeof(*list);
    list++;
  }
  *(char **)dest = NULL;
  dest += sizeof(*list);
  list = list_start;
  if (h_length > 0) { // Each item is an h_addr of length h_length
    while (*list != NULL) {
      memcpy(dest, *list, h_length);
      *dest_ptr = dest;
      dest_ptr++;
      dest += h_length;
      list++;
    }
  } else if (h_length == 0) { // else it's a string
    while (*list != NULL) {
      int len = strlen(*list);
      // FIXME:  Where is 'end' for struct hostent_result?
      char *end = dest + 10000;  // FIXME
      if (dest + len < end) {
        strcpy(dest, *list);
        *dest_ptr = dest;
        dest_ptr++;
        dest += (len + 1);
        list++;
      } else {
        errno = ERANGE;
        exit(ERANGE);
      }
    }
  }
  *dest_p = dest; 
}

int main(int argc, char **argv) {
  struct hostent *result = gethostbyname(argv[1]);
  if (result == NULL) {
    perror("gethostbyname");
    hostent_result.hostent.h_name = NULL;
  } else {
    ssize_t count = sizeof(result);
    hostent_result.here = (char*)&hostent_result.here; // record addr in child
    // h_addrtype, h_length: were filled in
    memcpy(&hostent_result.hostent, result, sizeof(struct hostent));
    // h_name
    char *tmp = hostent_result.padding;
    strncpy(tmp, hostent_result.hostent.h_name, sizeof(hostent_result.padding));
    hostent_result.hostent.h_name = tmp;
    tmp += strlen(hostent_result.hostent.h_name) + 1;
    // h_aliases
    hostent_result.hostent.h_aliases = (char**)tmp;
    copy_pointer_list(&tmp, result->h_aliases, 0);
    // h_addr_list
    hostent_result.hostent.h_addr_list = (char**)tmp;
    copy_pointer_list(&tmp, result->h_addr_list, result->h_length);

    writeall(1, &hostent_result, sizeof(hostent_result));
  }
  return 0;
}
