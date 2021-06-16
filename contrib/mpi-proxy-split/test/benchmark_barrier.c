#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

void *aux_thread(void *arg) {
}

int main(int argc, char *argv[])
{
    int rank, nprocs;
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);

    MPI_Barrier(MPI_COMM_WORLD);

    printf("Entering  %d of %d\n", rank, nprocs);fflush(stdout);
    time_t start = time(NULL);
    int i;
    for (i = 0; i < 1000000000; i++) {
      MPI_Barrier(MPI_COMM_WORLD);
      if (i % 10000 == 0) {
        int done = 0;
        if (rank == 0 && time(NULL)-start > 20) {
          done = 1;
        }
        MPI_Bcast(&done, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (done) {
          break;
        }
      }
    }
    time_t end = time(NULL);
    if (rank == 0) {
      printf("Time: %d seconds; count: %d; rate: %d/sec\n",
             end-start, i, i/(end-start));
      printf("Everyone should be entered by now. If not then Test Failed!\n");
      fflush(stdout);
    }
    MPI_Finalize();
    return 0;
}
