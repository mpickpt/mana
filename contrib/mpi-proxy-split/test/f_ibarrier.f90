PROGRAM hello_world_mpi
include 'mpif.h'

integer process_Rank, size_Of_Cluster, ierror, tag, flag
integer request, status(MPI_STATUS_SIZE)
logical dummy

call MPI_INIT(ierror)
call MPI_COMM_SIZE(MPI_COMM_WORLD, size_Of_Cluster, ierror)
call MPI_COMM_RANK(MPI_COMM_WORLD, process_Rank, ierror)
call MPI_IBARRIER(MPI_COMM_WORLD, request, ierror)
dummy = .true.
do while (dummy)
end do
call MPI_Wait(request, status, ierror)

print *, 'Hello World from process: ', process_Rank, 'of ', size_Of_Cluster

call MPI_FINALIZE(ierror)
END PROGRAM
