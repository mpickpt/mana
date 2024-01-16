/****************************************************************************
 *  Copyright (C) 2019-2020 by Twinkle Jain, Rohan garg, and Gene Cooperman *
 *  jain.t@husky.neu.edu, rohgarg@ccs.neu.edu, gene@ccs.neu.edu             *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"
#include "logging.h"

unsigned long
mtcp_strtol (char *str)
{
  unsigned long int v = 0;
  int base = 10;
  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    str += 2;
    base = 16;
  } else if (str[0] == '0') {
    str += 1;
    base = 8;
  } else {
    base = 10;
  }

  while (*str != '\0') {
    int c;
    if ((*str >= '0') && (*str <= '9')) c = *str - '0';
    else if ((*str >= 'a') && (*str <= 'f')) c = *str + 10 - 'a';
    else if ((*str >= 'A') && (*str <= 'F')) c = *str + 10 - 'A';
    else {
      DLOG(ERROR, "Error converting str to int\n");
    }
    v = v * base + c;
    str++;
  }
  return v;
}


ssize_t
writeAll(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *)buf;
  size_t num_written = 0;

  do {
    ssize_t rc = write(fd, ptr + num_written, count - num_written);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        return rc;
      }
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_written += rc;
    }
  } while (num_written < count);
  return num_written;
}

extern "C" ssize_t
readAll(int fd, void *buf, size_t count)
{
  char *ptr = (char *)buf;
  size_t num_read = 0;

  for (num_read = 0; num_read < count;) {
    ssize_t rc = read(fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        return -1;
      }
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_read += rc;
    }
  }
  return num_read;
}

int
checkLibrary(int fd, const char* name,
             char* glibcFullPath, size_t size)
{
  char procPath[PATH_MAX] = {0};
  char fullPath[PATH_MAX] = {0};
  snprintf(procPath, sizeof procPath, "/proc/self/fd/%d", fd);
  ssize_t len = readlink(procPath, fullPath, sizeof fullPath);
  if (len < 0) {
    DLOG(ERROR, "Failed to get path for %s. Error: %s\n",
         procPath, strerror(errno));
    return 0;
  }
  if (strstr(fullPath, name)) {
    strncpy(glibcFullPath, fullPath, size);
    return 1;
  }
  return 0;
}
