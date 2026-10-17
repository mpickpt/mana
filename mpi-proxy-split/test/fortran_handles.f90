!****************************************************************************
!*   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
!*   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
!*                                                                          *
!*  This file is part of DMTCP.                                             *
!*                                                                          *
!*  DMTCP is free software: you can redistribute it and/or                  *
!*  modify it under the terms of the GNU Lesser General Public License as   *
!*  published by the Free Software Foundation, either version 3 of the      *
!*  License, or (at your option) any later version.                         *
!*                                                                          *
!*  DMTCP is distributed in the hope that it will be useful,                *
!*  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
!*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
!*  GNU Lesser General Public License for more details.                     *
!*                                                                          *
!*  You should have received a copy of the GNU Lesser General Public        *
!*  License in the files COPYING and COPYING.LESSER.  If not, see           *
!*  <http://www.gnu.org/licenses/>.                                         *
!*****************************************************************************

! Fortran handles of objects that the program creates, between two ranks.
! Before the loop, it creates a communicator with MPI_COMM_SPLIT (the ranks
! in reverse order), one with MPI_COMM_DUP (with MPI_ERRORS_RETURN), a group,
! a vector datatype, and a struct datatype from an array of datatypes.  Each
! iteration uses them, waits for requests with MPI_WAITALL and MPI_WAITANY,
! and creates and frees a communicator and a datatype, which must then be
! MPI_COMM_NULL and MPI_DATATYPE_NULL.  Stops as the C tests do (see
! mana_test.h); then rank 0 prints "fortran_handles: PASS".

program fortran_handles
  use iso_c_binding, only: c_int
  implicit none
  include 'mpif.h'

  interface
    integer(c_int) function usleep(usec) bind(c, name='usleep')
      import :: c_int
      integer(c_int), value :: usec
    end function usleep
  end interface

  integer, parameter :: n = 64
  integer :: ierr, rank, nprocs, other, it, iterations, i, r, k, idx
  integer :: rev, rev_rank, dup, world_group, other_group, every_other, pair
  integer :: tmp_comm, tmp_type, m, result
  integer :: types(2), lengths(2), req(2), statuses(MPI_STATUS_SIZE, 2)
  integer :: status(MPI_STATUS_SIZE), ranks(1)
  integer(kind=MPI_ADDRESS_KIND) :: displacements(2)
  integer :: sbuf(n), rbuf(n)
  character(len=32) :: arg
  character(len=4096) :: control
  integer(8) :: last = -1

  it = 0
  call MPI_INIT(ierr)
  call MPI_COMM_RANK(MPI_COMM_WORLD, rank, ierr)
  call MPI_COMM_SIZE(MPI_COMM_WORLD, nprocs, ierr)
  if (nprocs /= 2) call fail('needs 2 ranks')
  other = 1 - rank

  iterations = -1
  do i = 1, command_argument_count() - 1
    call get_command_argument(i, arg)
    if (arg == '-n') then
      call get_command_argument(i + 1, arg)
      read (arg, *) iterations
    end if
  end do
  call get_environment_variable('MT_CONTROL', control)

  call MPI_COMM_SPLIT(MPI_COMM_WORLD, 0, nprocs - rank, rev, ierr)
  if (ierr /= MPI_SUCCESS .or. rev == MPI_COMM_NULL) &
    call fail('MPI_COMM_SPLIT failed')
  call MPI_COMM_DUP(MPI_COMM_WORLD, dup, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_DUP failed')
  call MPI_COMM_SET_ERRHANDLER(dup, MPI_ERRORS_RETURN, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_SET_ERRHANDLER failed')
  call MPI_COMM_GROUP(MPI_COMM_WORLD, world_group, ierr)
  ranks(1) = other
  call MPI_GROUP_INCL(world_group, 1, ranks, other_group, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_GROUP_INCL failed')
  ! Words 1, 3, 5, ...
  call MPI_TYPE_VECTOR(n / 2, 1, 2, MPI_INTEGER, every_other, ierr)
  call MPI_TYPE_COMMIT(every_other, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_TYPE_VECTOR failed')
  ! Words 1 and 3.
  types = (/ MPI_INTEGER, MPI_INTEGER /)
  lengths = (/ 1, 1 /)
  displacements = (/ 0_MPI_ADDRESS_KIND, 8_MPI_ADDRESS_KIND /)
  call MPI_TYPE_CREATE_STRUCT(2, lengths, displacements, types, pair, ierr)
  call MPI_TYPE_COMMIT(pair, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_TYPE_CREATE_STRUCT failed')

  do while (more())
    if (mod(it, 2) == rank) r = usleep(1000_c_int)

    ! A blocking exchange in 'rev', of every other word.
    call MPI_COMM_RANK(rev, rev_rank, ierr)
    if (rev_rank /= nprocs - 1 - rank) call fail('wrong rank in rev')
    do i = 1, n
      sbuf(i) = val(rank, it, i)
    end do
    rbuf = -1
    if (rank == 0) then
      call MPI_SEND(sbuf, 1, every_other, 1 - rev_rank, 1, rev, ierr)
      call MPI_RECV(rbuf, 1, every_other, 1 - rev_rank, 1, rev, status, ierr)
    else
      call MPI_RECV(rbuf, 1, every_other, 1 - rev_rank, 1, rev, status, ierr)
      call MPI_SEND(sbuf, 1, every_other, 1 - rev_rank, 1, rev, ierr)
    end if
    if (ierr /= MPI_SUCCESS) call fail('exchange in rev failed')
    do i = 1, n
      if ((mod(i, 2) == 1 .and. rbuf(i) /= val(other, it, i)) .or. &
          (mod(i, 2) == 0 .and. rbuf(i) /= -1)) &
        call fail('wrong value (exchange in rev)')
    end do

    ! Non-blocking exchanges in 'dup', of 'pair'.
    do k = 1, 2
      do i = 1, n
        sbuf(i) = val(rank, it, k * n + i)
      end do
      rbuf = -1
      call MPI_IRECV(rbuf, 1, pair, other, 2, dup, req(1), ierr)
      call MPI_ISEND(sbuf, 1, pair, other, 2, dup, req(2), ierr)
      if (k == 1) then
        call MPI_WAITALL(2, req, statuses, ierr)
        if (ierr /= MPI_SUCCESS) call fail('MPI_WAITALL failed')
      else
        do i = 1, 2
          call MPI_WAITANY(2, req, idx, status, ierr)
          if (ierr /= MPI_SUCCESS .or. idx < 1 .or. idx > 2) &
            call fail('MPI_WAITANY failed')
          if (req(idx) /= MPI_REQUEST_NULL) call fail('request not freed')
        end do
        call MPI_WAITANY(2, req, idx, status, ierr)
        if (idx /= MPI_UNDEFINED) call fail('MPI_WAITANY: no MPI_UNDEFINED')
      end if
      if (any(req /= MPI_REQUEST_NULL)) call fail('requests not freed')
      if (rbuf(1) /= val(other, it, k * n + 1) .or. rbuf(2) /= -1 .or. &
          rbuf(3) /= val(other, it, k * n + 3) .or. rbuf(4) /= -1) &
        call fail('wrong value (exchange in dup)')
    end do

    ! The maximum in 'rev'.
    sbuf(1) = val(rank, it, 0)
    call MPI_ALLREDUCE(sbuf, rbuf, 1, MPI_INTEGER, MPI_MAX, rev, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_ALLREDUCE failed')
    if (rbuf(1) /= max(val(0, it, 0), val(1, it, 0))) call fail('wrong max')

    call MPI_GROUP_SIZE(other_group, m, ierr)
    ranks(1) = 0
    call MPI_GROUP_TRANSLATE_RANKS(other_group, 1, ranks, world_group, r, &
                                   ierr)
    if (m /= 1 .or. r /= other) call fail('wrong group')
    call MPI_COMM_COMPARE(dup, MPI_COMM_WORLD, result, ierr)
    if (result /= MPI_CONGRUENT) call fail('dup is not MPI_CONGRUENT')

    if (mod(it, 4) == 0) then
      call MPI_COMM_DUP(rev, tmp_comm, ierr)
      call MPI_BARRIER(tmp_comm, ierr)
      if (ierr /= MPI_SUCCESS) call fail('MPI_BARRIER failed (tmp_comm)')
      call MPI_COMM_FREE(tmp_comm, ierr)
      if (tmp_comm /= MPI_COMM_NULL) call fail('freed comm not null')
      call MPI_TYPE_CONTIGUOUS(3, MPI_INTEGER, tmp_type, ierr)
      call MPI_TYPE_COMMIT(tmp_type, ierr)
      call MPI_TYPE_SIZE(tmp_type, m, ierr)
      if (m /= 12) call fail('wrong size of tmp_type')
      call MPI_TYPE_FREE(tmp_type, ierr)
      if (tmp_type /= MPI_DATATYPE_NULL) call fail('freed type not null')
    end if

    call MPI_BARRIER(MPI_COMM_WORLD, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_BARRIER failed')
    it = it + 1
  end do

  call MPI_TYPE_FREE(pair, ierr)
  call MPI_TYPE_FREE(every_other, ierr)
  call MPI_GROUP_FREE(other_group, ierr)
  call MPI_GROUP_FREE(world_group, ierr)
  call MPI_COMM_FREE(dup, ierr)
  call MPI_COMM_FREE(rev, ierr)
  if (rev /= MPI_COMM_NULL .or. other_group /= MPI_GROUP_NULL .or. &
      pair /= MPI_DATATYPE_NULL) call fail('freed handles not null')
  call MPI_BARRIER(MPI_COMM_WORLD, ierr)
  if (rank == 0) then
    write (*, '(a, i0, a)') 'fortran_handles: PASS (', it, ' iterations)'
    flush (6)
  end if
  call MPI_FINALIZE(ierr)

contains

  ! Like mt_continue() in mana_test.h.
  logical function more()
    integer(8) :: now, rate
    integer :: u, stop_at, ios
    if (len_trim(control) > 0) then
      call system_clock(now, rate)
      if (last < 0 .or. now - last >= rate / 10 .or. now < last) then
        last = now
        write (*, '(a, i0, a, i0)') 'fortran_handles: rank ', rank, &
          ': iteration ', it
        flush (6)
        if (iterations < 0) then
          open (newunit=u, file=trim(control) // '/stop', status='old', &
                action='read', iostat=ios)
          if (ios == 0) then
            read (u, *, iostat=ios) stop_at
            close (u)
            if (ios == 0) then
              if (stop_at < it) call fail('told to stop at a past iteration')
              iterations = stop_at
            end if
          end if
        end if
      end if
    end if
    more = iterations < 0 .or. it < iterations
  end function more

  ! Like mt_value() in mana_test.h, kept within 32 bits.
  integer function val(r, it, i)
    integer, intent(in) :: r, it, i
    val = r * 1000003 + mod(it, 100000) * 7919 + i
  end function val

  subroutine fail(msg)
    character(len=*), intent(in) :: msg
    integer :: e
    write (0, '(a, i0, a, i0, 2a)') 'fortran_handles: rank ', rank, &
      ': iteration ', it, ': ', msg
    flush (0)
    call MPI_ABORT(MPI_COMM_WORLD, 1, e)
    stop 1  ! MPI_ABORT may return before the job is killed.
  end subroutine fail

end program fortran_handles
