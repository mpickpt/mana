/*
  Test for the MPI_Ibcast method

  Run with >2 ranks for non-trivial results
  Run with -i [iterations] for specific number of iterations, defaults to 5
  Run with -1 or -2 to test specific cases (see below)

  CANNOT be run with mana_test.py (currently)
*/

#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int max_iterations;
#define SLEEP_PER_ITERATION 5
#define NUM_RANKS 4

#define MPI_TEST
#ifndef MPI_TEST
#define MPI_WAIT
#endif

void
usage(int argc, char *argv[])
{
  printf("Usage: %s [-12]\n"
         "Options:\n"
         "  -1: senders advances to the next call while receivers are at "
         "previous\n"
         "  -2: senders finish the last call while receivers are at previous "
         "calls\n",
         argv[0]);
  return;
}

void
test_case_0(void)
{
  int i, iter, myid;
  int root, count;
  int buffer[4];
  int expected_output[4];
  MPI_Status status;
  MPI_Request request = MPI_REQUEST_NULL;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (myid == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  root = 0;

  // Case 0 - all ranks likely reaches the current
  // iteration broadcast at the checkpoint
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (i = 0; i < NUM_RANKS; i++) {
      expected_output[i] = i + iterations;
      if (myid == root) {
        buffer[i] = expected_output[i];
      } else {
        buffer[i] = 0; // MPI_Ibcast should populate this.
      }
    }
    MPI_Ibcast(buffer, NUM_RANKS, MPI_INT, root, MPI_COMM_WORLD, &request);
    printf("[Rank = %d]", myid);
    sleep(SLEEP_PER_ITERATION);

#ifdef MPI_TEST
    while (1) {
      int flag = 0;
      MPI_Test(&request, &flag, &status);
      if (flag) {
        break;
      }
    }
#endif
#ifdef MPI_WAIT
    MPI_Wait(&request, &status);
#endif

    for (i = 0; i < NUM_RANKS; i++) {
      printf(" %d %d", buffer[i], expected_output[i]);
      assert(buffer[i] == expected_output[i]);
    }
    printf("\n");
    fflush(stdout);
  }
  MPI_Finalize();
}

// Case 1 - In a regression bug, the sender could advance to the next Ibcast
// while the receivers are the current or previous Ibcast at the
// checkpoint time.
void
test_case_1(void)
{
  int i, myid;
  int root, iterations;
  int buffer[4];
  int expected_output[4];
  MPI_Status status;
  MPI_Request request = MPI_REQUEST_NULL;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (myid == 0) {
    printf("Running test for %d iterations\n", max_iterations);
  }

  root = 0;
  for (int iterations = 0; iterations < max_iterations; iterations++) {
    for (i = 0; i < NUM_RANKS; i++) {
      expected_output[i] = i + iterations;
      if (myid == root) {
        buffer[i] = expected_output[i];
      } else {
        buffer[i] = 0; // MPI_Ibcast should populate this.
      }
    }
    // Sender (root) advances to the next Ibcast while receivers
    // wait a second.
    if (myid != root) {
      sleep(SLEEP_PER_ITERATION);
    }

    MPI_Ibcast(buffer, NUM_RANKS, MPI_INT, root, MPI_COMM_WORLD, &request);
    printf("[Rank = %d]", myid);

#ifdef MPI_TEST
    while (1) {
      int flag = 0;
      MPI_Test(&request, &flag, &status);
      if (flag) {
        break;
      }
    }
#endif
#ifdef MPI_WAIT
    MPI_Wait(&request, &status);
#endif

    for (i = 0; i < NUM_RANKS; i++) {
      printf(" %d %d", buffer[i], expected_output[i]);
      assert(buffer[i] == expected_output[i]);
    }
    printf("\n");
    fflush(stdout);
  }
  MPI_Finalize();
}

// Case 2 - In a regression bug, the sender could complete the last call while
// receivers are still at the current or previous calls while checkpointing.
void
test_case_2(void)
{
  int i, myid, root;
  int buffer[4];
  int expected_output[4];
  MPI_Status status;
  MPI_Request request = MPI_REQUEST_NULL;

  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  root = 0;
  for (i = 0; i < NUM_RANKS; i++) {
    expected_output[i] = i;
    if (myid == root) {
      buffer[i] = expected_output[i];
    } else {
      buffer[i] = 0; // MPI_Ibcast should populate this.
    }
  }

  // Checkpoint likely happens here - sender (root) advances to
  // the next Ibcast while receivers is waiting.
  if (myid != root) {
    printf("[Rank = %d] sleep %d seconds\n", myid, SLEEP_PER_ITERATION);
    fflush(stdout);
    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Ibcast(buffer, NUM_RANKS, MPI_INT, root, MPI_COMM_WORLD, &request);
  printf("[Rank = %d]", myid);
#ifdef MPI_TEST
  while (1) {
    int flag = 0;
    MPI_Test(&request, &flag, &status);
    if (flag) {
      break;
    }
  }
#endif
#ifdef MPI_WAIT
  MPI_Wait(&request, &status);
#endif
  for (i = 0; i < NUM_RANKS; i++) {
    printf(" %d %d", buffer[i], expected_output[i]);
    assert(buffer[i] == expected_output[i]);
  }
  printf("\n");
  fflush(stdout);
  if (myid == root) {
    printf("[Rank = %d] sleep %d seconds\n", myid, SLEEP_PER_ITERATION);
    fflush(stdout);
    sleep(SLEEP_PER_ITERATION);
  }
  MPI_Finalize();
}

int
main(int argc, char *argv[])
{
  // Parse runtime argument
  int opt;
  max_iterations = 5;
  int test = 0;

  while ((opt = getopt(argc, argv, "12i:")) != -1) {
    switch (opt) {
      case 'i':
        if (optarg != NULL) {
          char *optarg_end;
          max_iterations = strtol(optarg, &optarg_end, 10);
          if (max_iterations != 0 && optarg_end - optarg == strlen(optarg))
            break;
        }
      case '1':
        test = 1;
        break;
      case '2':
        test = 2;
        break;
      case '?':
      default:
        usage(argc, argv);
        return 1;
    }
  }
  // Default: test case 0. No argument.
  switch (test) {
    case 1:
      test_case_1();
      break;
    case 2:
      test_case_2();
      break;
    default:
      test_case_0();
  }

  return 0;
}
