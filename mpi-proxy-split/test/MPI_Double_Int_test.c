#include <stdio.h>
#include <mpi.h>

/*
 * MPICH treats MPI_DOUBLE_INT  as a negative 32-bit integer, 
 * signifying its 'struct' nature.
 * MANA has defined look-up table for pre-defined MPI_Datatypes as 
 * map<int64_t, int64_t> to accomodate for OpenMPI library.
 *
 * But, When handeling negative 64-bit value in MPICh with int32_t MPI_Datatypes, 
 * conversion back to 64-bit results in loss of upper-bits, violating integrity of argument passed.
 *
 * This tests for a possible regression.
 */

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  struct {double value; int rank;} local_data, global_data;

  // assign a value (eg: some computational result)
  local_data.value = rank * 1.5;    // different value for each rank
  local_data.rank = rank;
/*
  volatile int dummy = 1;
  printf("Dummy Hit!\n");
  while(dummy);
  */
  MPI_Allreduce(&local_data, &global_data, 1, MPI_DOUBLE_INT, MPI_MAXLOC, MPI_COMM_WORLD);

  // print result
  printf("[Rank %d]: Max value = %f from rank %d\n", rank, global_data.value, global_data.rank);

  MPI_Finalize();
  return 0;

}

