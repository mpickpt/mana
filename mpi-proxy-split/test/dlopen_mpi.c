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

// Opens the MPI library with dlopen() by its full path, as a program that
// loads MPI at run time does, and gets the MPI functions with dlsym() on the
// handle.  Runs MPI_Allreduce for SECONDS (time for a checkpoint).  Under
// MANA (libmana.so is loaded), the handle must be libmpistub.so, the
// functions libmana.so's, and the upper half must have no MPI library.  The
// program is not linked with the MPI library.
// usage: dlopen_mpi LIBRARY SECONDS

#define _GNU_SOURCE  // For dladdr() and dlinfo()
#include <dlfcn.h>
#include <link.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int rank = -1;

static void
fail(const char *what, const char *detail)
{
  fprintf(stderr, "dlopen_mpi: rank %d: %s%s\n", rank, what, detail);
  exit(1);
}

static const char *
base_name(const char *path)
{
  const char *slash = strrchr(path, '/');
  return slash != NULL ? slash + 1 : path;
}

static const char *library;  // The MPI library: argv[1]
static int has_mana, has_library;

// dl_iterate_phdr() callback: looks for libmana.so and the MPI library.
static int
find_objects(struct dl_phdr_info *info, size_t size, void *data)
{
  (void)size;
  (void)data;
  const char *name = base_name(info->dlpi_name);
  has_mana |= strcmp(name, "libmana.so") == 0;
  has_library |= strcmp(name, base_name(library)) == 0;
  return 0;
}

static void *
lookup(void *handle, const char *name, int under_mana)
{
  void *f = dlsym(handle, name);
  Dl_info info;
  if (f == NULL) {
    fail("dlsym: ", name);
  }
  if (under_mana && (dladdr(f, &info) == 0 ||
                     strcmp(base_name(info.dli_fname), "libmana.so") != 0)) {
    fail("not a function of libmana.so: ", name);
  }
  return f;
}

int
main(int argc, char **argv)
{
  if (argc != 3) {
    fail("usage: dlopen_mpi LIBRARY SECONDS", "");
  }
  library = argv[1];
  void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    fail("dlopen: ", dlerror());
  }
  struct link_map *map;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0) {
    fail("dlinfo: ", dlerror());
  }
  dl_iterate_phdr(find_objects, NULL);
  int under_mana = has_mana;
  if (under_mana && strcmp(base_name(map->l_name), "libmpistub.so") != 0) {
    fail("the handle is not libmpistub.so: ", map->l_name);
  }
  if (under_mana && has_library) {
    fail("the upper half has the MPI library: ", library);
  }

  int (*init)(int *, char ***) = lookup(handle, "MPI_Init", under_mana);
  int (*comm_rank)(MPI_Comm, int *) =
    lookup(handle, "MPI_Comm_rank", under_mana);
  int (*comm_size)(MPI_Comm, int *) =
    lookup(handle, "MPI_Comm_size", under_mana);
  int (*allreduce)(const void *, void *, int, MPI_Datatype, MPI_Op,
                   MPI_Comm) = lookup(handle, "MPI_Allreduce", under_mana);
  int (*bcast)(void *, int, MPI_Datatype, int, MPI_Comm) =
    lookup(handle, "MPI_Bcast", under_mana);
  int (*finalize)(void) = lookup(handle, "MPI_Finalize", under_mana);

  int size;
  init(&argc, &argv);
  comm_rank(MPI_COMM_WORLD, &rank);
  comm_size(MPI_COMM_WORLD, &size);
  time_t end = time(NULL) + (time_t)atof(argv[2]);
  int stop = 0;
  for (int it = 0; !stop; it++) {
    int x = rank + it, sum = -1;
    allreduce(&x, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (sum != size * (size - 1) / 2 + size * it) {
      fail("wrong sum", "");
    }
    usleep(10000);
    stop = time(NULL) >= end;
    bcast(&stop, 1, MPI_INT, 0, MPI_COMM_WORLD);
  }
  finalize();
  if (rank == 0) {
    printf("dlopen_mpi: PASS\n");
    fflush(stdout);
  }
  return 0;
}
