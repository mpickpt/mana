/*
  Test for the MPI_Type_create_struct method

  Run with any ranks
  Defaults to 10000 iterations
  Intended to be run with mana_test.py

  Inspired by:
    http://mpi.deino.net/mpi_functions/MPI_Type_create_struct.html
*/

#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

struct Partstruct {
    char c;
    double d[6];
    char b[7];
};

int main(int argc, char **argv) {
    int max_iterations = 10000;
    if (argc > 1) max_iterations = atoi(argv[1]);

    int rank, comm_size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

    if (rank == 0) {
        printf("Running Partstruct test with %d ranks and %d iterations\n", comm_size, max_iterations);
    }

    MPI_Datatype partstruct_type;
    
    int block_lengths[3] = {1, 6, 7};
    MPI_Datatype types[3] = {MPI_CHAR, MPI_DOUBLE, MPI_CHAR};
    MPI_Aint offsets[3];

    offsets[0] = offsetof(struct Partstruct, c);
    offsets[1] = offsetof(struct Partstruct, d);
    offsets[2] = offsetof(struct Partstruct, b);

    MPI_Type_create_struct(3, block_lengths, offsets, types, &partstruct_type);
    MPI_Type_commit(&partstruct_type);

    for (int i = 0; i < max_iterations; i++) {
        struct Partstruct send_p;
        struct Partstruct recv_p;

        send_p.c = 'A' + (i % 26);
        for (int j = 0; j < 6; j++) send_p.d[j] = (double)i + j;
        snprintf(send_p.b, 7, "iter%d", i % 100);

        int dest = (rank + 1) % comm_size;
        int source = (rank - 1 + comm_size) % comm_size;

        MPI_Sendrecv(&send_p, 1, partstruct_type, dest, 0,
                     &recv_p, 1, partstruct_type, source, 0,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (comm_size == 1) {
            assert(recv_p.c == send_p.c);
            for (int j = 0; j < 6; j++) assert(recv_p.d[j] == send_p.d[j]);
            assert(strcmp(recv_p.b, send_p.b) == 0);
        }

        if (rank == 0 && i % 1000 == 999) {
            printf("Completed %d iterations\n", i + 1);
            fflush(stdout);
        }
    }

    if (rank == 0) printf("Test finished successfully.\n");

    MPI_Type_free(&partstruct_type);
    MPI_Finalize();
    return EXIT_SUCCESS;
}