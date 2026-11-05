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

// Blocks that realloc() grows (#539).  glibc's realloc() of a block that
// malloc() mapped calls mremap(), which grows the mapping in place if the
// range above it is free, and otherwise moves it.  At start, each rank
// mallocs 64 MB and grows it to 128 MB in place, and maps 256 MB, which
// must not overlap it.  It also mallocs two more 64 MB blocks and grows the
// lower one to 128 MB, which moves it.  Each iteration, across checkpoints
// and restarts, checks words in the grown blocks and in the mapping.

#include <stdint.h>
#include <sys/mman.h>
#include "mana_test.h"

#define BLOCK (64L << 20)
#define MAPPING (256L << 20)
#define PAGE 4096
#define SAMPLES 64

static uint64_t
word(uint64_t base, size_t off)
{
  return base ^ (off * 0x9e3779b97f4a7c15ULL);
}

static void
fill(char *p, size_t len, uint64_t base)
{
  for (size_t off = 0; off < len; off += sizeof(uint64_t)) {
    *(uint64_t *)(p + off) = word(base, off);
  }
}

// Checks SAMPLES words of [from, from + len), and the last one.
static void
check(const char *what, char *p, size_t from, size_t len, uint64_t base,
      long it)
{
  for (int i = 0; i <= SAMPLES; i++) {
    size_t off = i < SAMPLES ? from + (len / SAMPLES) * i + (it % 512) * 8
                             : from + len - sizeof(uint64_t);
    MT_CHECK(*(uint64_t *)(p + off) == word(base, off),
             "iteration %ld: %s: wrong word at offset %#zx", it, what, off);
  }
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "realloc_grow");
  uint64_t base = 0x5555 + mt_rank;
  char *p = malloc(BLOCK);
  MT_CHECK(p != NULL, "%s", "malloc");
  fill(p, BLOCK, base);
  char *q = realloc(p, 2 * BLOCK);
  MT_CHECK(q != NULL, "%s", "realloc");
  check("grown block", q, 0, BLOCK, base, 0);
  fill(q, 2 * BLOCK, base);
  char *m = mmap(NULL, MAPPING, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  MT_CHECK(m != MAP_FAILED, "%s", "mmap");
  MT_CHECK(m + MAPPING <= q || m >= q + 2 * BLOCK,
           "mmap %p-%p overlaps the block %p-%p", (void *)m,
           (void *)(m + MAPPING), (void *)q, (void *)(q + 2 * BLOCK));
  fill(m, MAPPING, ~base);
  char *lower = malloc(BLOCK);
  char *upper = malloc(BLOCK);
  MT_CHECK(lower != NULL && upper != NULL, "%s", "malloc");
  fill(lower, BLOCK, base + 2);
  fill(upper, BLOCK, base + 3);
  char *moved = realloc(lower, 2 * BLOCK);
  MT_CHECK(moved != NULL, "%s", "realloc");
  check("moved block", moved, 0, BLOCK, base + 2, 0);
  fill(moved, 2 * BLOCK, base + 2);
  long it;
  for (it = 0; mt_continue(it); it++) {
    usleep(1000);
    check("block", q, 0, 2 * BLOCK, base, it);
    check("grown half of the block", q, BLOCK, BLOCK, base, it);
    check("mapping", m, 0, MAPPING, ~base, it);
    check("moved block", moved, 0, 2 * BLOCK, base + 2, it);
    check("block above the moved one", upper, 0, BLOCK, base + 3, it);
    MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
  }
  mt_finish(it);
  return 0;
}
