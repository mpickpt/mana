! Tested on Cori with:  ftn -c THIS_FILE.f90
! C binding: https://gcc.gnu.org/onlinedocs/gfortran/Interoperable-Subroutines-and-Functions.html
! MPICH: integer(c_int),bind(C, name=" MPIR_F08_MPI_IN_PLACE ") & ::  MPI_IN_PLACE
! FIXME: Make this cleaner, either use bind() command, or use an out parameter.

! NOTE:  In MPI, the FORTRAN constants can have different values from
!         one process to the next.  So, we need to discover the
!         FORTRAN constants in each new MPI process, to recognize
!         them at C level.
! For example, note that the MPI_Allreduce wrapper contains:
!   if (sendbuf == FORTRAN_MPI_IN_PLACE) {
!     sendbuf = MPI_IN_PLACE;
!   }
! MPI 3.1 standard:
!   The constants that cannot be used in initialization expressions or
!   assignments in Fortran are as follows:
!   MPI_BOTTOM
!   MPI_STATUS_IGNORE
!   MPI_STATUSES_IGNORE
!   MPI_ERRCODES_IGNORE
!   MPI_IN_PLACE
!   MPI_ARGV_NULL
!   MPI_ARGVS_NULL
!   MPI_UNWEIGHTED
!   MPI_WEIGHTS_EMPTY


      subroutine get_fortran_constants()
        implicit none
        include 'mpif.h'

        ! explicit interfaces
        interface
          subroutine get_fortran_constants_helper(t)
            implicit none
            integer,intent(in) :: t
          end subroutine get_fortran_constants_helper
          subroutine get_fortran_arrays_ignore(t)
            implicit none
            integer :: t(*)
          end subroutine get_fortran_arrays_ignore
        end interface
        ! These must match the list in get_fortran_constants.c
        call get_fortran_constants_helper(MPI_BOTTOM)
        ! MPI_STATUS_IGNORE is a struct, similar to a length-1 array
        call get_fortran_arrays_helper(MPI_STATUS_IGNORE)
        call get_fortran_arrays_helper(MPI_STATUSES_IGNORE)
        call get_fortran_arrays_helper(MPI_ERRCODES_IGNORE)
        call get_fortran_constants_helper(MPI_IN_PLACE)
        ! FIXME: MPI_ARGV_NULL is a CHARACTER(1), not supported in MANA
        ! call get_fortran_constants_helper(MPI_ARGV_NULL)
        ! FIXME: MPI_ARGV_NULL is a CHARACTER(1) in mpich-gnu, not supported
        ! in MANA
        ! call get_fortran_arrays_helper(MPI_ARGVS_NULL)
      end subroutine get_fortran_constants
