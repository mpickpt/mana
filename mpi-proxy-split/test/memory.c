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

// Memory that MANA must save and restore.  Each rank keeps a ring of up to
// 32 malloc'd blocks of 64 B to 4 MB (from the heap, or mmap'd by malloc)
// and up to 4 anonymous mappings of 1 to 8 MB, a quarter of whose pages are
// never written.  Each iteration frees the oldest block after checking all
// of it and allocates a new one; every few iterations it also grows or
// shrinks the newest block with realloc(), and replaces the oldest mapping.
// Each iteration checks a few words of every live block and mapping, and
// the two ranks exchange a checksum of these words.

#include <stdint.h>
#include <sys/mman.h>
#include "mana_test.h"

#define NBLOCKS 32
#define NREGIONS 4
#define HEAP_MAX (48L << 20)  // Bytes in live blocks
#define REGION_KEY (1ULL << 40)
#define GOLDEN 0x9e3779b97f4a7c15ULL

// A block or a mapping.  Its byte i holds byte i % 8 of word(rank, key,
// i / 8), except in a mapping's skipped pages, which hold zeros.
typedef struct {
  char *p;
  size_t size;
  uint64_t key;
  int region;
} area;

static area blocks[NBLOCKS], regions[NREGIONS];
static long first_block, next_block;    // Live blocks: [first, next)
static long first_region, next_region;  // Live mappings: [first, next)
static size_t heap_bytes, page;

static uint64_t
mix(uint64_t x)
{
  x += GOLDEN;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

static uint64_t
word(int rank, uint64_t key, size_t w)
{
  return mix(key ^ (uint64_t)rank << 48) ^ w * GOLDEN;
}

static int
skipped(const area *a, size_t pg)
{
  return a->region && (pg + a->key) % 4 == 3;
}

// The word that 'rank' has at byte 'off' (a multiple of 8) of area 'a'.
static uint64_t
expect(int rank, const area *a, size_t off)
{
  return skipped(a, off / page) ? 0 : word(rank, a->key, off / 8);
}

// Fills bytes [from, to) of an area with its pattern, or checks them.
// Returns the offset of the first wrong byte, or -1.
static long
pattern(const area *a, size_t from, size_t to, int check)
{
  uint64_t base = mix(a->key ^ (uint64_t)mt_rank << 48);
  size_t i = from;
  while (i < to) {
    uint64_t v = base ^ (i / 8) * GOLDEN;
    uint64_t *q = (uint64_t *)(a->p + i);
    int full = i % 8 == 0 && i + 8 <= to;
    if (full && !check) {
      *q = v;
      i += 8;
    } else if (full && *q == v) {
      i += 8;
    } else {
      // A partial word, or a wrong one: byte by byte.
      unsigned char b = v >> (8 * (i % 8));
      if (!check) {
        a->p[i] = b;
      } else if ((unsigned char)a->p[i] != b) {
        return i;
      }
      i++;
    }
  }
  return -1;
}

// Fills or checks a whole area, page by page for a mapping.
static long
whole(const area *a, int check)
{
  if (!a->region) {
    return pattern(a, 0, a->size, check);
  }
  for (size_t pg = 0; pg < a->size / page; pg++) {
    long bad = -1;
    if (!skipped(a, pg)) {
      bad = pattern(a, pg * page, (pg + 1) * page, check);
    } else if (check) {
      for (size_t i = pg * page; i < (pg + 1) * page && bad < 0; i++) {
        bad = a->p[i] ? (long)i : -1;
      }
    }
    if (bad >= 0) {
      return bad;
    }
  }
  return -1;
}

static void
check_whole(const area *a, long id)
{
  long bad = whole(a, 1);
  MT_CHECK(bad < 0, "%s %ld (%zu bytes): byte %ld is 0x%x",
           a->region ? "mapping" : "block", id, a->size, bad,
           (unsigned char)a->p[bad]);
}

// 64 B to 4 MB, about as many in [2^k, 2^(k+1)) for each k.
static size_t
block_size(uint64_t key)
{
  uint64_t h = mix(key);
  size_t s = (size_t)64 << (h % 16);
  return s + (h >> 8) % s;
}

static void
free_block(void)
{
  area *a = &blocks[first_block % NBLOCKS];
  check_whole(a, first_block++);
  heap_bytes -= a->size;
  free(a->p);
}

static void
new_block(void)
{
  size_t size = block_size(next_block);
  while (next_block - first_block == NBLOCKS ||
         heap_bytes + size > HEAP_MAX) {
    free_block();
  }
  area *a = &blocks[next_block % NBLOCKS];
  a->key = next_block;
  a->size = size;
  a->p = malloc(a->size);
  MT_CHECK(a->p != NULL, "malloc(%zu)", a->size);
  heap_bytes += a->size;
  whole(a, 0);
  next_block++;
}

// Gives the newest block a new size; realloc() keeps the old bytes.
static void
resize_block(long it)
{
  area *a = &blocks[(next_block - 1) % NBLOCKS];
  size_t old = a->size;
  a->size = block_size(a->key + (uint64_t)it * NBLOCKS * NBLOCKS);
  a->p = realloc(a->p, a->size);
  MT_CHECK(a->p != NULL, "realloc(%zu)", a->size);
  heap_bytes += a->size - old;
  long bad = pattern(a, 0, old < a->size ? old : a->size, 1);
  MT_CHECK(bad < 0, "block %ld after realloc from %zu to %zu bytes: byte "
           "%ld is 0x%x", next_block - 1, old, a->size, bad,
           (unsigned char)a->p[bad]);
  if (a->size > old) {
    pattern(a, old, a->size, 0);
  }
}

static void
free_region(void)
{
  area *a = &regions[first_region % NREGIONS];
  check_whole(a, first_region++);
  MT_CHECK(munmap(a->p, a->size) == 0, "munmap(%zu)", a->size);
}

static void
new_region(void)
{
  if (next_region - first_region == NREGIONS) {
    free_region();
  }
  area *a = &regions[next_region % NREGIONS];
  a->key = REGION_KEY + next_region;
  a->region = 1;
  a->size = (1 << 20) + mix(a->key) % (7 << 20);
  a->size = (a->size + page - 1) / page * page;
  a->p = mmap(NULL, a->size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  MT_CHECK(a->p != MAP_FAILED, "mmap(%zu)", a->size);
  whole(a, 0);
  next_region++;
}

static uint64_t
sample_area(uint64_t sum, int rank, long it, const area *a, long id)
{
  size_t off[4] = {0, a->size / 2, a->size - 8, it * 4104 % (a->size - 7)};
  for (int i = 0; i < 4; i++) {
    uint64_t v = expect(rank, a, off[i] & ~7UL);
    if (rank == mt_rank) {
      uint64_t got = *(uint64_t *)(a->p + (off[i] & ~7UL));
      MT_CHECK(got == v, "iteration %ld: %s %ld: word at byte %zu is "
               "0x%llx, not 0x%llx", it, a->region ? "mapping" : "block",
               id, off[i] & ~7UL, (unsigned long long)got,
               (unsigned long long)v);
    }
    sum = sum * 1000003 + v;
  }
  return sum;
}

// Checks a few words of every live area, and returns their checksum.  For
// the other rank, returns what its checksum must be: both ranks have
// areas of the same sizes.
static uint64_t
sample(int rank, long it)
{
  uint64_t sum = it;
  for (long id = first_block; id < next_block; id++) {
    sum = sample_area(sum, rank, it, &blocks[id % NBLOCKS], id);
  }
  for (long id = first_region; id < next_region; id++) {
    sum = sample_area(sum, rank, it, &regions[id % NREGIONS], id);
  }
  return sum;
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "memory");
  MT_CHECK(mt_size == 2, "needs 2 ranks, not %d", mt_size);
  page = sysconf(_SC_PAGESIZE);
  int peer = 1 - mt_rank;
  long it;
  for (it = 0; mt_continue(it); it++) {
    new_block();
    if (it % 5 == 2) {
      resize_block(it);
    }
    if (it % 3 == 0) {
      new_region();
    }
    uint64_t mine = sample(mt_rank, it), theirs;
    if (it % 2 == mt_rank) {
      usleep(1000);
    }
    MT_MPI(MPI_Sendrecv(&mine, 1, MPI_UINT64_T, peer, 0, &theirs, 1,
                        MPI_UINT64_T, peer, 0, MPI_COMM_WORLD,
                        MPI_STATUS_IGNORE));
    MT_CHECK(theirs == sample(peer, it), "iteration %ld: checksum 0x%llx "
             "from rank %d", it, (unsigned long long)theirs, peer);
  }
  while (first_block < next_block) {
    free_block();
  }
  while (first_region < next_region) {
    free_region();
  }
  mt_finish(it);
  return 0;
}
