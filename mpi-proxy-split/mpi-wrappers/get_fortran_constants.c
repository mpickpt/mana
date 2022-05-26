#include <stdio.h>
#include <stdlib.h>

// In C, we call get_fortran_constants(), which eventually calls
// get_fortran_constants_helper(FORTRAN_CONSTANT) which calls
//  get_fortran_constants_helper-(int *t) in this file, which allows
//  us to capture in C the value of FORTRAN_CONSTANT.
// In FORTRAN, a constant is implemented as a variable whose value
//  is a pointer to the address containing the constant.
// See the wrapper for MPI_Allreduce, using FORTRAN_MPI_STATUSES_IGNORE
//  for an example of how these constants are used in MANA.

// Quoting from the MPI standard:
// In Fortran the implementation of these special constants may
// require the use of language constructs that are outside the Fortran
// standard. Using special values for the constants (e.g., by defining them
// through parameter statements) is not possible because an implementation
// cannot distinguish these values from legal data. Typically, these
// constants are implemented as predefined static variables (e.g., a
// variable in an MPI-declared COMMON block), relying on the fact that
// the target compiler passes data by address. Inside the subroutine, this
// address can be extracted by some mechanism outside the Fortran standard
// (e.g., by Fortran extensions or by implementing the function in C).

// MPI 3.1 standard:
//   The constants that cannot be used in initialization expressions or
//   assignments in Fortran are as follows:
void *FORTRAN_MPI_BOTTOM = NULL;
void *FORTRAN_MPI_STATUS_IGNORE = NULL;
void *FORTRAN_MPI_STATUSES_IGNORE = NULL;
void *FORTRAN_MPI_ERRCODES_IGNORE = NULL;
void *FORTRAN_MPI_IN_PLACE = NULL;
void *FORTRAN_MPI_ARGV_NULL = NULL;
void *FORTRAN_MPI_ARGVS_NULL = NULL;
void *FORTRAN_MPI_UNWEIGHTED = NULL;
void *FORTRAN_MPI_WEIGHTS_EMPTY = NULL;
void *FORTRAN_CONSTANTS_END = NULL;

// These must match the list in fortran_constants.f90
void **fortran_constants[] = {
  &FORTRAN_MPI_BOTTOM,
  &FORTRAN_MPI_STATUS_IGNORE,
  &FORTRAN_MPI_STATUSES_IGNORE,
  &FORTRAN_MPI_ERRCODES_IGNORE,
  &FORTRAN_MPI_IN_PLACE,
  // FIXME: MPI_ARGV_NULL is a CHARACTER(1), not supported in MANA
  // &FORTRAN_MPI_ARGV_NULL,
  &FORTRAN_MPI_ARGVS_NULL,
  &FORTRAN_MPI_UNWEIGHTED,
  &FORTRAN_MPI_WEIGHTS_EMPTY,
  &FORTRAN_CONSTANTS_END
};

// This is called as fortran_constants.f90:get_fortran_constants_helper()
void get_fortran_constants_helper_(int *t) {
  static int iter = 0;
  if (iter == -1) {
    fprintf(stderr, "MANA: get_fortran_constants_helper_: Internal error\n");
    exit(1);
  }
  *(fortran_constants[iter++]) = t;
  if (fortran_constants[iter] == &FORTRAN_CONSTANTS_END) {
    iter = -1; // no more FOFTRAN constants to initialize.
  }
}

// This is called as fortran_constants.f90:get_fortran_arrays_helper()
void get_fortran_arrays_helper_(int *t) {
  get_fortran_constants_helper_(t);
}

void get_fortran_constants_(void);

void get_fortran_constants() {
  static int initialized = 0;
  if (!initialized) {
    // This was defined in fortran_constants.f90:get_fortran_constants()
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
