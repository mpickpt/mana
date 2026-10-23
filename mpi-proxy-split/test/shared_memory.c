/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
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
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// Tests POSIX shared memory that the application maps.  shm_open() creates
// this memory as a file in /dev/shm.  MPI libraries map files in /dev/shm,
// too, but in the lower half, which MANA does not save.  Each rank fills an
// object of its own once.  In every iteration, it then checks a few words of
// the object, and the ranks exchange a checksum of these words.

#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "mana_test.h"

#define SIZE (4L << 20)
#define WORDS (SIZE / sizeof(int))
#define SAMPLES 16

static char name[64];
static int *mem;

// Returns the word at 'index' of the object of 'rank'.
static int
word(int rank, size_t index)
{
  return mt_value(rank, 0, (int)index);
}

static size_t
sample(long iteration, int s)
{
  return (iteration * 7919L + s * 104729L) % WORDS;
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "shared_memory");
  snprintf(name, sizeof(name), "/mana_test_shm.%d.%d", (int)getpid(),
           mt_rank);
  int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  MT_CHECK(fd >= 0, "shm_open(%s)", name);
  MT_CHECK(ftruncate(fd, SIZE) == 0, "ftruncate(%s)", name);
  mem = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  MT_CHECK(mem != MAP_FAILED, "mmap(%s)", name);
  close(fd);
  for (size_t i = 0; i < WORDS; i++) {
    mem[i] = word(mt_rank, i);
  }

  long it;
  for (it = 0; mt_continue(it); it++) {
    long sum = 0, expected = 0;
    for (int s = 0; s < SAMPLES; s++) {
      size_t i = sample(it, s);
      MT_CHECK(mem[i] == word(mt_rank, i), "word %zu of %s: %d", i, name,
               mem[i]);
      sum += mem[i];
      for (int r = 0; r < mt_size; r++) {
        expected += word(r, i);
      }
    }
    long total;
    MT_MPI(MPI_Allreduce(&sum, &total, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD));
    MT_CHECK(total == expected, "checksum %ld, expected %ld", total,
             expected);
    usleep(1000);
  }
  munmap(mem, SIZE);
  shm_unlink(name);
  mt_finish(it);
  return 0;
}
