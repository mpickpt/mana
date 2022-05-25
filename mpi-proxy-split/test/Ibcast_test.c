/*
  Example code for Bcast (not used here):
   https://mpitutorial.com/tutorials/mpi-broadcast-and-collective-communication/
   https://github.com/mpitutorial/mpitutorial/blob/gh-pages/tutorials/mpi-broadcast-and-collective-communication/code/my_bcast.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
#include <getopt.h>
/************************************************************
This is a simple Ibcast program in MPI
OPTIONS:   iterations, MPI_TEST, MPI_WAIT, Move the 'sleep(1)'
************************************************************/

#define MPI_TEST
#ifndef MPI_TEST
# define MPI_WAIT
#endif

void usage(int argc, char *argv[])
{
  printf("Usage: %s [-12]\n"
         "Options:\n"
	 "  -1:	senders advances to the next call while receivers are at previous\n"
	 "  -2: senders finishes the last call while receivers are at previous calls\n",
	 argv[0]);
  return;
}

void test_case_0(void) {
    int i,iter,myid;
    int root,count,iterations;
    int buffer[4];
    int expected_output[4];
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL;

    MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);
    root = 0;
    count = 4;
    iterations = 100;
    // Case 0 - all ranks likely reaches the current
    // iteration broadcast at the checkpoint
    for (iter = 0; iter < iterations; iter++) {
        for (i=0; i<count; i++) {
            expected_output[i] = i + iter;
            if (myid == root) {
              buffer[i] = expected_output[i];
            } else {
              buffer[i] = 0; // MPI_Ibcast should populate this.
            }
        }
        MPI_Ibcast(buffer,count,MPI_INT,root,MPI_COMM_WORLD, &request);
        printf("[Rank = %d]", myid);
        sleep(1); // The checkpoint is likely to occur here.
#ifdef MPI_TEST
        while (1) {
            int flag = 0;
            MPI_Test(&request, &flag, &status);
            if (flag) { break; }
        }
#endif
#ifdef MPI_WAIT
        MPI_Wait(&request, &status);
#endif
        for (i=0;i<count;i++) {
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
void test_case_1(void) {
    int i,iter,myid;
    int root,count,iterations;
    int buffer[4];
    int expected_output[4];
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL;

    MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);
    root = 0;
    count = 4;
    iterations = 100;
    for (iter = 0; iter < iterations; iter++) {
        for (i=0; i<count; i++) {
          expected_output[i] = i + iter;
          if (myid == root) {
            buffer[i] = expected_output[i];
          } else {
            buffer[i] = 0; // MPI_Ibcast should populate this.
          }
        }
	// Sender (root) advances to the next Ibcast while receivers
	// wait a second.
	if (myid != root) {
	  sleep(1);
	}
        MPI_Ibcast(buffer,count,MPI_INT,root,MPI_COMM_WORLD, &request);
        printf("[Rank = %d]", myid);
#ifdef MPI_TEST
        while (1) {
          int flag = 0;
          MPI_Test(&request, &flag, &status);
          if (flag) { break; }
        }
#endif
#ifdef MPI_WAIT
        MPI_Wait(&request, &status);
#endif
        for (i=0;i<count;i++) {
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
void test_case_2(void) {
    int i,count,myid,root;
    int buffer[4];
    int expected_output[4];
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL;

    MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);
    root = 0;
    count = 4;
    for (i=0; i<count; i++) {
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
        printf("[Rank = %d] sleep 100 seconds\n", myid);
        fflush(stdout);
        sleep(100);
    }
    MPI_Ibcast(buffer,count,MPI_INT,root,MPI_COMM_WORLD, &request);
    printf("[Rank = %d]", myid);
#ifdef MPI_TEST
    while (1) {
        int flag = 0;
        MPI_Test(&request, &flag, &status);
        if (flag) { break; }
    }
#endif
#ifdef MPI_WAIT
    MPI_Wait(&request, &status);
#endif
    for (i=0;i<count;i++) {
        printf(" %d %d", buffer[i], expected_output[i]);
        assert(buffer[i] == expected_output[i]);
    }
    printf("\n");
    fflush(stdout);
    if (myid == root) {
        printf("[Rank = %d] sleep 100 seconds\n", myid);
        fflush(stdout);
        sleep(100);
    }

    MPI_Finalize();
}

int main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "12")) != -1) {
        switch(opt) {
	case '1':
          test_case_1();
          break;
        case '2':
          test_case_2();
	  break;
        case '?':
	default:
          usage(argc, argv);
          return 1;
	}
    }
    // Default: test case 0. No argument.
    if (opt == -1 && argc == 1) {
      test_case_0();
    }

    return 0;
}
