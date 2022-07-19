/*
  Test for the MPI_Scatterv method

  Run with >2 ranks for non-trivial results
  Defaults to 10000 iterations
  Intended to be run with mana_test.py
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESSES 10

int
main(int argc, char **argv)
{
  // Parse runtime argument
  int max_iterations = 10000; // default
  if (argc != 1) {
    max_iterations = atoi(argv[1]);
  }

  int rank, size, i, j;
  int table[MAX_PROCESSES][MAX_PROCESSES];
  int row[MAX_PROCESSES];
  int errors = 0;
  int participants;
  int displs[MAX_PROCESSES];
  int send_counts[MAX_PROCESSES];

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (rank == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  /* A maximum of MAX_PROCESSES processes can participate */
  if (size > MAX_PROCESSES)
    participants = MAX_PROCESSES;
  else
    participants = size;
  if (rank < participants) {
    for (int iterations = 0; iterations < max_iterations; iterations++) {
      int recv_count = MAX_PROCESSES;

      /* If I'm the root (process 0), then fill out the big
      table and setup send_counts and displs arrays */
      if (rank == 0)
        for (i = 0; i < participants; i++) {
          send_counts[i] = recv_count;
          displs[i] = i * MAX_PROCESSES;
          for (j = 0; j < MAX_PROCESSES; j++)
            table[i][j] = i + j;
        }

      /* Scatter the big table to everybody's little table */
      int ret = MPI_Scatterv(&table[0][0], send_counts, displs, MPI_INT,
                             &row[0], recv_count, MPI_INT, 0, MPI_COMM_WORLD);
      assert(ret == MPI_SUCCESS);

      /* Now see if our row looks right */
      for (i = 0; i < MAX_PROCESSES; i++) {
        printf("[Rank =%d] recved = %d \t expected = %d\n", rank, row[i],
               i + rank);
        fflush(stdout);
        assert(row[i] == i + rank);
      }

      for (i = 0; i < MAX_PROCESSES; i++) {
        row[i] = 0;
      }
    }
  }

  MPI_Finalize();
  return 0;
}
