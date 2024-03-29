/*
  Test for the MPI_Cart_sub method

  Must run with 6 ranks
  Defaults to 10 iterations
  Intended to be run with mana_test.py

  Source: http://site.sci.hkbu.edu.hk/tdgc/tutorial/ParallelProgrammingWithMPI
               /examples/ch08_cart_sub_example.c
*/
#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SLEEP_PER_ITERATION 5

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int max_iterations = 10; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int nrow, mcol, i, lastrow, p, root;
  int Iam, id2D, colID, ndim;
  int coords1D[2], coords2D[2], dims[2], aij[1], alocal[3];
  int belongs[2], periods[2], reorder;
  MPI_Comm comm2D, commcol;
  /* Starts MPI processes ... */
  MPI_Init(&argc, &argv); /* starts MPI */
  MPI_Comm_rank(MPI_COMM_WORLD, &Iam); /* get current process id */
  MPI_Comm_size(MPI_COMM_WORLD, &p); /* get number of processes */
  assert(p == 6);
  if (Iam == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  nrow = 3;
  mcol = 2;
  ndim = 2;
  root = 0;
  periods[0] = 1;
  periods[1] = 0;
  reorder = 1;

  /* create cartesian topology for processes */
  dims[0] = nrow; /* number of rows */
  dims[1] = mcol; /* number of columns */
  MPI_Cart_create(MPI_COMM_WORLD, ndim, dims, periods, reorder, &comm2D);
  MPI_Comm_rank(comm2D, &id2D);
  MPI_Cart_coords(comm2D, id2D, ndim, coords2D);

  for (int iterations = 0; iterations < max_iterations; iterations++) {
    /* Create 1D column subgrids */
    belongs[0] = 1; /* this dimension belongs to subgrid */
    belongs[1] = 0;
    int ret = MPI_Cart_sub(comm2D, belongs, &commcol);
    assert(ret == MPI_SUCCESS);
    MPI_Comm_rank(commcol, &colID);
    MPI_Cart_coords(commcol, colID, 1, coords1D);

    sleep(SLEEP_PER_ITERATION);
    ret = MPI_Barrier(MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);

    /* aij = (i+1)*10 + j + 1; 1 matrix element to each proc */
    aij[0] = (coords2D[0] + 1) * 10 + coords2D[1] + 1;

    if (Iam == root) {
      printf("\n     MPI_Cart_sub example:");
      printf("\n 3x2 cartesian grid ==> 2 (3x1) column subgrids\n");
      printf("\n   Iam     2D       2D          1D       1D      aij");
      printf("\n  Rank   Rank     coords.     Rank  coords.\n");
      fflush(stdout);
    }

    /* Last element of each column gathers elements of its own column */
    for (i = 0; i <= nrow - 1; i++) {
      alocal[i] = -1;
    }

    lastrow = nrow - 1;
    ret = MPI_Gather(aij, 1, MPI_INT, alocal, 1, MPI_INT, lastrow, commcol);
    assert(ret == MPI_SUCCESS);

    ret = MPI_Barrier(MPI_COMM_WORLD);
    assert(ret == MPI_SUCCESS);

    printf("%6d|%6d|%6d %6d|%6d|%8d|", Iam, id2D, coords2D[0], coords2D[1],
           colID, coords1D[0]);
    fflush(stdout);
    for (i = 0; i <= lastrow; i++) {
      printf("%6d ", alocal[i]);
    }
    printf("\n");
    fflush(stdout);

    if (Iam == 4) {
      assert(alocal[0] == 11);
      assert(alocal[1] == 21);
      assert(alocal[2] == 31);
    }
    if (Iam == 5) {
      assert(alocal[0] == 12);
      assert(alocal[1] == 22);
      assert(alocal[2] == 32);
    }
  }

  MPI_Finalize(); /* let MPI finish up ...  */
}
