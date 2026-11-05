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

// Derived datatypes, between two ranks.  Ten long-lived types are made at
// start; the last one is made from two derived types that are then freed.
// Each iteration exchanges one message per type and checks every element
// and every gap of the receive buffer.  Each iteration also makes a
// short-lived type and frees it while the messages that use it are pending.

#include "mana_test.h"

#define NTYPES 10
#define MAXE 16    // Elements per instance of a type
#define BUF 1024
#define FILL 0x5a  // Gaps of the send buffer
#define GAP 0xa5   // Receive buffer before the receive

// A datatype and the layout that the test expects of it.
typedef struct {
  const char *name;
  MPI_Datatype type;
  int count;            // Instances per message
  int n;                // Elements per instance, in type map order
  MPI_Aint off[MAXE];   // Byte offset of each element
  char kind[MAXE];      // 'i' (int) or 'd' (double)
  int single;           // All elements are of one kind
  int size;
  MPI_Aint lb, extent;
} layout;

static layout types[NTYPES];
static double sbuf_d[BUF / 8], rbuf_d[BUF / 8];
static char *sbuf = (char *)sbuf_d, *rbuf = (char *)rbuf_d;

static int
esize(char kind)
{
  return kind == 'd' ? 8 : 4;
}

// Adds 'len' consecutive elements at byte 'off'.
static void
add(layout *l, char kind, MPI_Aint off, int len)
{
  for (int i = 0; i < len; i++) {
    l->kind[l->n] = kind;
    l->off[l->n++] = off + i * esize(kind);
  }
}

// Sets size, lb and extent from the elements (no explicit bounds).
static void
bounds(layout *l)
{
  MPI_Aint lo = l->off[0], hi = 0;
  l->size = 0;
  l->single = 1;
  for (int i = 0; i < l->n; i++) {
    MPI_Aint end = l->off[i] + esize(l->kind[i]);
    l->size += esize(l->kind[i]);
    l->single = l->single && l->kind[i] == l->kind[0];
    lo = l->off[i] < lo ? l->off[i] : lo;
    hi = end > hi ? end : hi;
  }
  l->lb = lo;
  l->extent = hi - lo;
}

static void
make_types(void)
{
  layout *l = types;

  l->name = "contiguous";
  l->count = 3;
  MT_MPI(MPI_Type_contiguous(5, MPI_INT, &l->type));
  add(l, 'i', 0, 5);
  bounds(l++);

  l->name = "vector";  // 4 blocks of 2 ints, stride 3 ints
  l->count = 2;
  MT_MPI(MPI_Type_vector(4, 2, 3, MPI_INT, &l->type));
  for (int b = 0; b < 4; b++) {
    add(l, 'i', b * 12, 2);
  }
  bounds(l++);

  l->name = "hvector";  // 3 blocks of 2 ints, stride 20 bytes
  l->count = 2;
  MT_MPI(MPI_Type_create_hvector(3, 2, 20, MPI_INT, &l->type));
  for (int b = 0; b < 3; b++) {
    add(l, 'i', b * 20, 2);
  }
  bounds(l++);

  // Blocks out of order: the type map follows the block order.
  int ilen[3] = {1, 3, 2}, idisp[3] = {5, 0, 10};
  l->name = "indexed";
  l->count = 2;
  MT_MPI(MPI_Type_indexed(3, ilen, idisp, MPI_INT, &l->type));
  for (int b = 0; b < 3; b++) {
    add(l, 'i', idisp[b] * 4, ilen[b]);
  }
  bounds(l++);

  *l = l[-1];
  l->name = "dup";
  MT_MPI(MPI_Type_dup(l[-1].type, &l->type));
  l++;

  int hlen[2] = {2, 1};
  MPI_Aint hdisp[2] = {16, 0};
  l->name = "hindexed";
  l->count = 2;
  MT_MPI(MPI_Type_create_hindexed(2, hlen, hdisp, MPI_DOUBLE, &l->type));
  for (int b = 0; b < 2; b++) {
    add(l, 'd', hdisp[b], hlen[b]);
  }
  bounds(l++);

  MPI_Aint bdisp[3] = {0, 12, 32};
  l->name = "hindexed_block";
  l->count = 2;
  MT_MPI(MPI_Type_create_hindexed_block(3, 2, bdisp, MPI_INT, &l->type));
  for (int b = 0; b < 3; b++) {
    add(l, 'i', bdisp[b], 2);
  }
  bounds(l++);

  // int, gap, 2 doubles, gap, int: 32 bytes, already a multiple of the
  // alignment of double.
  int slen[3] = {1, 2, 1};
  MPI_Aint sdisp[3] = {0, 8, 28};
  MPI_Datatype stype[3] = {MPI_INT, MPI_DOUBLE, MPI_INT};
  l->name = "struct";
  l->count = 3;
  MT_MPI(MPI_Type_create_struct(3, slen, sdisp, stype, &l->type));
  add(l, 'i', 0, 1);
  add(l, 'd', 8, 2);
  add(l, 'i', 28, 1);
  bounds(l++);

  l->name = "resized";
  l->count = 5;
  MT_MPI(MPI_Type_create_resized(MPI_INT, -4, 12, &l->type));
  add(l, 'i', 0, 1);
  bounds(l);
  l->lb = -4;
  l->extent = 12;
  l++;

  // inner: ints at 0 and 8, extent 12 (size 8).  middle: 3 inners, stride
  // 2 extents of inner (24 bytes).  Both are freed; chain lives on.
  MPI_Datatype inner, middle;
  l->name = "chain";
  l->count = 2;
  MT_MPI(MPI_Type_vector(2, 1, 2, MPI_INT, &inner));
  MT_MPI(MPI_Type_vector(3, 1, 2, inner, &middle));
  MT_MPI(MPI_Type_free(&inner));
  MT_MPI(MPI_Type_create_resized(middle, 0, 64, &l->type));
  MT_MPI(MPI_Type_free(&middle));
  for (int b = 0; b < 3; b++) {
    add(l, 'i', b * 24, 1);
    add(l, 'i', b * 24 + 8, 1);
  }
  bounds(l);
  l->extent = 64;

  for (int t = 0; t < NTYPES; t++) {
    MT_MPI(MPI_Type_commit(&types[t].type));
  }
}

static void
check_bounds(const layout *l)
{
  int size;
  MPI_Aint lb, extent;
  MT_MPI(MPI_Type_size(l->type, &size));
  MT_MPI(MPI_Type_get_extent(l->type, &lb, &extent));
  MT_CHECK(size == l->size && lb == l->lb && extent == l->extent,
           "%s: size %d lb %ld extent %ld", l->name, size, (long)lb,
           (long)extent);
}

static int
value(int rank, long it, int t, int k)
{
  return mt_value(rank, it, t * 1000 + k);
}

// Byte offset of element k of a message: 'packed' if it was received as
// an array of its basic type.
static MPI_Aint
offset(const layout *l, int k, int packed)
{
  if (packed) {
    return (MPI_Aint)k * esize(l->kind[0]);
  }
  return (k / l->n) * l->extent + l->off[k % l->n];
}

static void
fill(const layout *l, int t, long it)
{
  memset(sbuf, FILL, BUF);
  for (int k = 0; k < l->count * l->n; k++) {
    int v = value(mt_rank, it, t, k);
    double d = v + 0.5;
    char kind = l->kind[k % l->n];
    MT_CHECK(offset(l, k, 0) + esize(kind) <= BUF, "%s: too big", l->name);
    memcpy(sbuf + offset(l, k, 0), kind == 'd' ? (void *)&d : (void *)&v,
           esize(kind));
  }
  memset(rbuf, GAP, BUF);
}

static void
check(const layout *l, int t, long it, int packed)
{
  int peer = 1 - mt_rank;
  char mask[BUF] = {0};
  for (int k = 0; k < l->count * l->n; k++) {
    MPI_Aint off = offset(l, k, packed);
    int v, want = value(peer, it, t, k);
    double d;
    if (l->kind[k % l->n] == 'd') {
      memcpy(&d, rbuf + off, sizeof(d));
      MT_CHECK(d == want + 0.5, "%s: iteration %ld element %d (byte %ld): %g",
               l->name, it, k, (long)off, d);
    } else {
      memcpy(&v, rbuf + off, sizeof(v));
      MT_CHECK(v == want, "%s: iteration %ld element %d (byte %ld): %d",
               l->name, it, k, (long)off, v);
    }
    memset(mask + off, 1, esize(l->kind[k % l->n]));
  }
  for (int i = 0; i < BUF; i++) {
    MT_CHECK(mask[i] || (unsigned char)rbuf[i] == GAP,
             "%s: iteration %ld: byte %d of a gap is 0x%x", l->name, it, i,
             (unsigned char)rbuf[i]);
  }
}

// Sends 'count' instances to the peer and receives as many; every other
// time, a type of a single kind is received as an array of that kind.
static void
exchange(const layout *l, int t, long it)
{
  int peer = 1 - mt_rank;
  int packed = l->single && (it + t) % 2;
  MPI_Datatype rtype = l->type;
  int rcount = l->count, count;
  MPI_Status status;
  if (packed) {
    rtype = l->kind[0] == 'd' ? MPI_DOUBLE : MPI_INT;
    rcount = l->count * l->n;
  }
  fill(l, t, it);
  MT_MPI(MPI_Sendrecv(sbuf, l->count, l->type, peer, t, rbuf, rcount, rtype,
                      peer, t, MPI_COMM_WORLD, &status));
  MT_MPI(MPI_Get_count(&status, rtype, &count));
  MT_CHECK(status.MPI_SOURCE == peer && status.MPI_TAG == t &&
           count == rcount, "%s: status: source %d tag %d count %d", l->name,
           status.MPI_SOURCE, status.MPI_TAG, count);
  check(l, t, it, packed);
}

// Makes a type (indexed or hindexed_block, its shape depends on the
// iteration), posts the receive and the send, and frees the type before
// they complete.  One rank sends late, so the other waits for a message
// of a freed type.
static void
short_lived(long it)
{
  int peer = 1 - mt_rank, late = it % 2 == mt_rank, t = NTYPES;
  layout l = {.name = "short-lived indexed", .count = 2};
  if (it % 2 == 0) {
    int len[3] = {1 + it % 3, 2, 1}, disp[3] = {4, 0, 9 + it % 4};
    MT_MPI(MPI_Type_indexed(3, len, disp, MPI_INT, &l.type));
    for (int b = 0; b < 3; b++) {
      add(&l, 'i', disp[b] * 4, len[b]);
    }
  } else {
    MPI_Aint disp[3] = {0, 16 + 8 * (it % 3), 48};
    l.name = "short-lived hindexed_block";
    MT_MPI(MPI_Type_create_hindexed_block(3, 2, disp, MPI_INT, &l.type));
    for (int b = 0; b < 3; b++) {
      add(&l, 'i', disp[b], 2);
    }
  }
  bounds(&l);
  MT_MPI(MPI_Type_commit(&l.type));
  check_bounds(&l);
  fill(&l, t, it);
  MPI_Request req[2];
  MT_MPI(MPI_Irecv(rbuf, l.count, l.type, peer, t, MPI_COMM_WORLD, &req[0]));
  if (late) {
    usleep(2000);
  }
  MT_MPI(MPI_Isend(sbuf, l.count, l.type, peer, t, MPI_COMM_WORLD, &req[1]));
  MT_MPI(MPI_Type_free(&l.type));
  MT_CHECK(l.type == MPI_DATATYPE_NULL, "%s: not freed", l.name);
  if (!late) {
    usleep(1000);
  }
  MT_MPI(MPI_Waitall(2, req, MPI_STATUSES_IGNORE));
  check(&l, t, it, 0);
}

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "datatypes");
  MT_CHECK(mt_size == 2, "needs 2 ranks, not %d", mt_size);
  make_types();
  long it;
  for (it = 0; mt_continue(it); it++) {
    if (it % 2 == mt_rank) {
      usleep(1000);
    }
    for (int t = 0; t < NTYPES; t++) {
      check_bounds(&types[t]);
      exchange(&types[t], t, it);
    }
    short_lived(it);
  }
  mt_finish(it);
  return 0;
}
