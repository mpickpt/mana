! Tested on Cori with:  ftn -c THIS_FILE.f90
! C binding: https://gcc.gnu.org/onlinedocs/gfortran/Interoperable-Subroutines-and-Functions.html
! MPICH: integer(c_int),bind(C, name=" MPIR_F08_MPI_IN_PLACE ") & ::  MPI_IN_PLACE
! FIXME: Make this cleaner, either use bind() command, or use an out parameter.


      subroutine get_fortran_constants()
        implicit none
        include 'mpif.h'

        ! explicit interfaces
        interface
          subroutine get_fortran_constants_helper(t)
            implicit none
            integer,intent(in) :: t
          end subroutine get_fortran_constants_helper
          subroutine get_fortran_mpi_statuses_ignore(t)
            implicit none
            integer :: t(*)
          end subroutine get_fortran_mpi_statuses_ignore
        end interface
        call get_fortran_constants_helper(MPI_IN_PLACE)
        call get_fortran_mpi_statuses_ignore(MPI_STATUSES_IGNORE)
      end subroutine get_fortran_constants
