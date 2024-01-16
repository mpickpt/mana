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

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <string.h>

#include <linux/limits.h>

// Logging levels
#define NOISE 3 // Verbose
#define INFO  2 // Informational logs
#define ERROR 1 // Error logs

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

static const char *colors[] = {KNRM, KRED, KGRN, KYEL};

#ifndef DEBUG_LEVEL
// Let's announce errors out loud
# define DEBUG_LEVEL 1
#endif // ifndef DEBUG_LEVEL

#define CLOG(LOG_LEVEL, fmt, ...)                                              \
      fprintf(stderr, "%s[%s +%d]: " fmt KNRM, colors[LOG_LEVEL], __FILE__,    \
              __LINE__, ##__VA_ARGS__);                                        \

#define DLOG(LOG_LEVEL, fmt, ...)                                              \
do {                                                                           \
  if (DEBUG_LEVEL) {                                                           \
    if (LOG_LEVEL <= DEBUG_LEVEL)                                              \
      CLOG(LOG_LEVEL, fmt, ##__VA_ARGS__)                                      \
  }                                                                            \
} while(0)

#endif // ifndef LOGGING_H
