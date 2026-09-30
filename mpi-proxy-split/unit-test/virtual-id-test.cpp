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

// Standalone test and microbenchmark of the virtual-ID table, without gtest.
// The real MPI library serves as the lower half (NEXT_FUNC(f) is MPI_f).
//
// Usage: mpirun -n 1 ./virtual-id-test.exe [--bench]

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <set>
#include <unordered_map>
#include <vector>

#include "lower-half-api.h"
#include "virtual_id.h"

// Definitions normally provided by the rest of the MANA plugin.
static LowerHalfInfo_t lh_info_storage;
LowerHalfInfo_t *lh_info = &lh_info_storage;
MPI_Comm g_world_comm;
int g_world_rank = 0;
std::unordered_map<unsigned int, unsigned long> seq_num;
std::unordered_map<unsigned int, unsigned long> target;
std::unordered_map<MPI_Comm, unsigned int> ggid_table;

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                   \
      failures++;                                                       \
    }                                                                   \
  } while (0)

// virtual_id.cpp asks p2p_log_replay.cpp whether a pending call uses a
// datatype; this test has no pending calls.
bool
pendingCallUsesDatatype(MPI_Datatype type)
{
  return false;
}

static void
init_lh_info()
{
  // The lower half is our MPI library, so its constants are ours.  The
  // Makefile extracts this file from lower-half.cpp.
#include "lh_constants.inc"
  lh_info->fsaddr = 0;  // No FS switch: JUMP_TO_LOWER_HALF does nothing.
}

// The predefined constants that the tests check against.
static const int64_t constants[] = {
  (int64_t)MPI_COMM_WORLD, (int64_t)MPI_COMM_SELF, (int64_t)MPI_COMM_NULL,
  (int64_t)MPI_GROUP_NULL, (int64_t)MPI_GROUP_EMPTY, (int64_t)MPI_REQUEST_NULL,
  (int64_t)MPI_OP_NULL, (int64_t)MPI_SUM, (int64_t)MPI_MAX, (int64_t)MPI_MIN,
  (int64_t)MPI_PROD, (int64_t)MPI_MAXLOC, (int64_t)MPI_MINLOC,
  (int64_t)MPI_REPLACE, (int64_t)MPI_NO_OP, (int64_t)MPI_DATATYPE_NULL,
  (int64_t)MPI_INT, (int64_t)MPI_DOUBLE, (int64_t)MPI_CHAR, (int64_t)MPI_BYTE,
  (int64_t)MPI_DOUBLE_INT, (int64_t)MPI_2INT, (int64_t)MPI_INTEGER,
  (int64_t)MPI_DOUBLE_PRECISION, (int64_t)MPI_INFO_ENV, (int64_t)MPI_INFO_NULL,
  (int64_t)MPI_ERRHANDLER_NULL, (int64_t)MPI_WIN_NULL,
  (int64_t)MPI_MESSAGE_NULL,
  (int64_t)MPI_MESSAGE_NO_PROC, (int64_t)MPI_ERRORS_RETURN,
  (int64_t)MPI_ERRORS_ARE_FATAL, (int64_t)MPI_UINT64_T, (int64_t)MPI_COUNT,
};

static bool
equals_constant(int handle)
{
  for (int64_t c : constants) {
    if ((int)c == handle) {
      return true;
    }
  }
  return false;
}

static void
test_constants()
{
  mana_mpi_handle h;
  CHECK(get_real_id((mana_mpi_handle){.comm = MPI_COMM_WORLD}).comm ==
        lh_info->MANA_COMM_WORLD);
  CHECK(get_real_id((mana_mpi_handle){.datatype = MPI_DOUBLE}).datatype ==
        lh_info->MANA_DOUBLE);
  CHECK(get_real_id((mana_mpi_handle){.op = MPI_SUM}).op == lh_info->MANA_SUM);
  CHECK(get_real_id((mana_mpi_handle){.request = MPI_REQUEST_NULL}).request ==
        lh_info->MANA_REQUEST_NULL);
  // A negative "struct" datatype whose upper 32 bits were lost (see
  // get_real_id_slow()): zero-extended instead of sign-extended.
  h._handle64 = (int64_t)(uint32_t)MPI_DOUBLE_INT;
  CHECK(get_real_id(h).datatype == lh_info->MANA_DOUBLE_INT);
  h._handle64 = (int64_t)MPI_DOUBLE_INT;
  CHECK(get_real_id(h).datatype == lh_info->MANA_DOUBLE_INT);
  CHECK(get_virt_id_desc((mana_mpi_handle){.comm = MPI_COMM_WORLD}) == NULL);
  CHECK(is_predefined_id((mana_mpi_handle){._handle64 = (int64_t)MPI_INT}));
}

static void
test_comm()
{
  MPI_Comm real;
  MPI_Comm_dup(MPI_COMM_WORLD, &real);
  MPI_Comm virt = new_virt_comm(real);
  CHECK(!is_predefined_id((mana_mpi_handle){.comm = virt}));
  CHECK(get_real_id((mana_mpi_handle){.comm = virt}).comm == real);
  mana_comm_desc *desc =
    (mana_comm_desc*)get_virt_id_desc((mana_mpi_handle){.comm = virt});
  CHECK(desc != NULL && desc->size == 1 && desc->global_ranks[0] == 0);
  MPI_Comm real2;
  MPI_Comm_dup(MPI_COMM_WORLD, &real2);
  update_virt_id((mana_mpi_handle){.comm = virt},
                 (mana_mpi_handle){.comm = real2});
  CHECK(get_real_id((mana_mpi_handle){.comm = virt}).comm == real2);
  free_virt_id((mana_mpi_handle){.comm = virt});
  MPI_Comm_free(&real);
  MPI_Comm_free(&real2);
}

static void
test_requests()
{
  const int n = 20000;
  std::vector<MPI_Request> virts;
  std::set<int> seen;
  for (int i = 0; i < n; i++) {
    // Fake real requests; only the table is exercised.
    MPI_Request v = new_virt_request((MPI_Request)(0x10000 + i));
    CHECK(!equals_constant(v));
    CHECK(!is_predefined_id((mana_mpi_handle){.request = v}));
    CHECK(seen.insert(v).second);
    virts.push_back(v);
  }
  for (int i = 0; i < n; i++) {
    CHECK(get_real_id((mana_mpi_handle){.request = virts[i]}).request ==
          (MPI_Request)(0x10000 + i));
    CHECK(get_virt_id_desc((mana_mpi_handle){.request = virts[i]}) == NULL);
  }
  // MPI_Irecv's fake request: a virtual request mapped to MPI_REQUEST_NULL.
  MPI_Request fake =
    new_virt_request((MPI_Request)((intptr_t)MPI_REQUEST_NULL + 1));
  update_virt_id((mana_mpi_handle){.request = fake},
                 (mana_mpi_handle){.request = MPI_REQUEST_NULL});
  CHECK(fake != MPI_REQUEST_NULL);
  CHECK(get_real_id((mana_mpi_handle){.request = fake}).request ==
        MPI_REQUEST_NULL);
  free_virt_id((mana_mpi_handle){.request = fake});
  for (int i = 0; i < n; i++) {
    free_virt_id((mana_mpi_handle){.request = virts[i]});
  }
#ifdef MANA_VIRT_ID_SLOT_MASK
  // Freed handles are stale: not found, even after their slots are reused.
  size_t live = virt_id_live_count();
  std::vector<MPI_Request> again;
  for (int i = 0; i < n; i++) {
    again.push_back(new_virt_request((MPI_Request)(0x20000 + i)));
  }
  for (int i = 0; i < n; i++) {
    CHECK(lookup_virt_id_entry((mana_mpi_handle){.request = virts[i]}) ==
          NULL);
    CHECK(get_real_id((mana_mpi_handle){.request = again[i]}).request ==
          (MPI_Request)(0x20000 + i));
  }
  for (int i = 0; i < n; i++) {
    free_virt_id((mana_mpi_handle){.request = again[i]});
  }
  CHECK(virt_id_live_count() == live);
#endif
}

#ifdef MANA_VIRT_ID_SLOT_MASK
// No handle may equal a predefined constant: with MPICH, MPI_SUM (0x58000003)
// looks like a request in slot 3, and MPI_INT (0x4c000405) like an op in slot
// 0x405, so such slots are never handed out.  Allocates many handles, then
// cycles the last slot through all its generations.
static void
test_no_constant_collision(int kind)
{
  std::vector<mana_mpi_handle> live;
  for (int i = 0; i < 0x1000; i++) {
    mana_mpi_handle h = add_virt_id((mana_mpi_handle){._handle64 = 1}, NULL,
                                    kind);
    CHECK(!equals_constant(h._handle));
    CHECK(!is_predefined_id(h));
    live.push_back(h);
  }
  std::set<int> handles;
  for (int i = 0; i < 4 * (MANA_VIRT_ID_GEN_MASK + 1); i++) {
    free_virt_id(live.back());
    live.pop_back();
    mana_mpi_handle h = add_virt_id((mana_mpi_handle){._handle64 = 2}, NULL,
                                    kind);
    CHECK(!equals_constant(h._handle));
    CHECK(!is_predefined_id(h));
    handles.insert(h._handle);
    live.push_back(h);
  }
  // The slot was reused with more than one generation.
  CHECK(handles.size() > 1);
  for (mana_mpi_handle h : live) {
    free_virt_id(h);
  }
}

// Many allocations with at most 64 live requests: the table must stay
// bounded and every handle must translate to its own real request.
static void
test_reuse_stress()
{
  const long n = 10000000;
  MPI_Request live[64];
  size_t base = virt_id_live_count();
  std::set<unsigned int> slots;
  for (int i = 0; i < 64; i++) {
    live[i] = new_virt_request((MPI_Request)(i + 1));
  }
  for (long i = 0; i < n; i++) {
    int k = (int)(i * 7 % 64);
    CHECK(get_real_id((mana_mpi_handle){.request = live[k]}).request ==
          (MPI_Request)(k + 1));
    free_virt_id((mana_mpi_handle){.request = live[k]});
    live[k] = new_virt_request((MPI_Request)(k + 1));
    slots.insert(live[k] & MANA_VIRT_ID_SLOT_MASK);
    if (equals_constant(live[k])) {
      CHECK(false);
      break;
    }
  }
  for (int i = 0; i < 64; i++) {
    free_virt_id((mana_mpi_handle){.request = live[i]});
  }
  CHECK(virt_id_live_count() == base);
  // Freed slots are reused: the loop cycles through a few slots only.
  CHECK(slots.size() <= 64);
}
#endif

/************************************************************
 * Microbenchmark
 ************************************************************/
static double
now_ns()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e9 + ts.tv_nsec;
}

// Keeps the compiler from hoisting the lookup out of the loop.
#define OPAQUE(x) asm volatile("" : "+r"(x))

static double
bench_get_real_id(mana_mpi_handle h, long n)
{
  int64_t sink = 0;
  double t = now_ns();
  for (long i = 0; i < n; i++) {
    int64_t v = h._handle64;
    OPAQUE(v);
    mana_mpi_handle hv;
    hv._handle64 = v;
    sink += get_real_id(hv)._handle64;
  }
  t = (now_ns() - t) / n;
  OPAQUE(sink);
  return t;
}

static void
bench()
{
  const long n = 50000000;
  MPI_Comm real;
  MPI_Comm_dup(MPI_COMM_WORLD, &real);
  MPI_Comm virt = new_virt_comm(real);
  // Some live handles, as in an application.
  std::vector<MPI_Request> reqs;
  for (int i = 0; i < 100; i++) {
    reqs.push_back(new_virt_request((MPI_Request)(0x30000 + i)));
  }
  mana_mpi_handle h;
  h._handle64 = 0;
  h.comm = MPI_COMM_WORLD;
  printf("BENCH,get_real_id(MPI_COMM_WORLD),%.2f,ns\n",
         bench_get_real_id(h, n));
  h._handle64 = 0;
  h.datatype = MPI_DOUBLE;
  printf("BENCH,get_real_id(MPI_DOUBLE),%.2f,ns\n", bench_get_real_id(h, n));
  h._handle64 = (int64_t)(uint32_t)MPI_DOUBLE_INT;
  printf("BENCH,get_real_id(MPI_DOUBLE_INT),%.2f,ns\n",
         bench_get_real_id(h, n));
  h._handle64 = 0;
  h.comm = virt;
  printf("BENCH,get_real_id(dup_comm),%.2f,ns\n", bench_get_real_id(h, n));
  h._handle64 = 0;
  h.request = reqs[50];
  printf("BENCH,get_real_id(request),%.2f,ns\n", bench_get_real_id(h, n));

  const long m = 10000000;
  double t = now_ns();
  for (long i = 0; i < m; i++) {
    MPI_Request r = new_virt_request((MPI_Request)(i + 1));
    OPAQUE(r);
    free_virt_id((mana_mpi_handle){.request = r});
  }
  printf("BENCH,new_virt_request+free_virt_id,%.2f,ns\n", (now_ns() - t) / m);
  for (MPI_Request r : reqs) {
    free_virt_id((mana_mpi_handle){.request = r});
  }
  free_virt_id((mana_mpi_handle){.comm = virt});
  MPI_Comm_free(&real);
}

int
main(int argc, char **argv)
{
  bool do_bench = argc > 1 && strcmp(argv[1], "--bench") == 0;
  MPI_Init(&argc, &argv);
  init_lh_info();
  init_predefined_virt_ids();

  if (do_bench) {
    bench();
  } else {
    test_constants();
#ifdef MANA_VIRT_ID_SLOT_MASK
    test_no_constant_collision(MANA_REQUEST_KIND);
    test_no_constant_collision(MANA_OP_KIND);
#endif
    test_comm();
    test_requests();
#ifdef MANA_VIRT_ID_SLOT_MASK
    test_reuse_stress();
#endif
    printf("virtual-id-test: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
  }
  MPI_Finalize();
  return failures != 0;
}
