#include <stdio.h>

void *FORTRAN_MPI_IN_PLACE = NULL;
void *FORTRAN_MPI_STATUSES_IGNORE = NULL;
void get_fortran_constants_();

void get_fortran_constants_helper_(int *t) {
  FORTRAN_MPI_IN_PLACE = t;
}

void get_fortran_mpi_statuses_ignore_(int *t) {
  FORTRAN_MPI_STATUSES_IGNORE = t;
}


void get_fortran_constants() {
  static int initialized = 0;
  if (!initialized) {
    get_fortran_constants_();
    initialized = 1;
  }
}

#ifdef STANDALONE
int main() {
  get_fortran_constants();
  printf("Fortran MPI_IN_PLACE = %p\n", FORTRAN_MPI_IN_PLACE);
  printf("Fortran MPI_STATUSES_IGNORE = %p\n", FORTRAN_MPI_STATUSES_IGNORE);
  return 0;
}
#endif

