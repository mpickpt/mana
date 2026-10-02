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

! Fortran point-to-point between two ranks.  Each iteration: a blocking
! exchange received with MPI_STATUS_IGNORE (which the MPI library must not
! write to), one received with a status, an MPI_ALLREDUCE with MPI_IN_PLACE
! and an MPI_BARRIER.  Stops as the C tests do (see mana_test.h); then rank
! 0 prints "fortran_p2p: PASS".

program fortran_p2p
  use iso_c_binding, only: c_int
  implicit none
  include 'mpif.h'

  interface
    integer(c_int) function usleep(usec) bind(c, name='usleep')
      import :: c_int
      integer(c_int), value :: usec
    end function usleep
  end interface

  integer, parameter :: n = 100
  integer :: ierr, rank, nprocs, other, it, iterations, i, r, m, count
  integer :: sbuf(n), rbuf(n), status(MPI_STATUS_SIZE)
  integer :: ignore(MPI_STATUS_SIZE)
  character(len=32) :: arg
  character(len=4096) :: control
  integer(8) :: last = -1

  it = 0
  call MPI_INIT(ierr)
  call MPI_COMM_RANK(MPI_COMM_WORLD, rank, ierr)
  call MPI_COMM_SIZE(MPI_COMM_WORLD, nprocs, ierr)
  if (nprocs /= 2) call fail('needs 2 ranks')
  other = 1 - rank
  ignore = MPI_STATUS_IGNORE  ! A receive must not change it.

  iterations = -1
  do i = 1, command_argument_count() - 1
    call get_command_argument(i, arg)
    if (arg == '-n') then
      call get_command_argument(i + 1, arg)
      read (arg, *) iterations
    end if
  end do
  call get_environment_variable('MT_CONTROL', control)

  do while (more())
    if (mod(it, 2) == rank) r = usleep(1000_c_int)

    ! Exchange 1: received with MPI_STATUS_IGNORE.
    do i = 1, n
      sbuf(i) = val(rank, it, i)
    end do
    rbuf = -1
    if (rank == 0) then
      call MPI_SEND(sbuf, n, MPI_INTEGER, other, 1, MPI_COMM_WORLD, ierr)
      call MPI_RECV(rbuf, n, MPI_INTEGER, other, 1, MPI_COMM_WORLD, &
                    MPI_STATUS_IGNORE, ierr)
    else
      call MPI_RECV(rbuf, n, MPI_INTEGER, other, 1, MPI_COMM_WORLD, &
                    MPI_STATUS_IGNORE, ierr)
      call MPI_SEND(sbuf, n, MPI_INTEGER, other, 1, MPI_COMM_WORLD, ierr)
    end if
    if (ierr /= MPI_SUCCESS) call fail('MPI_RECV failed')
    if (any(MPI_STATUS_IGNORE /= ignore)) &
      call fail('MPI_RECV wrote to MPI_STATUS_IGNORE')
    do i = 1, n
      if (rbuf(i) /= val(other, it, i)) call fail('wrong value (exchange 1)')
    end do

    ! Exchange 2: m words, received with a status.
    m = n / 2 + mod(it, n / 2)
    do i = 1, m
      sbuf(i) = val(rank, it, n + i)
    end do
    rbuf = -1
    status = -1
    if (rank == 0) then
      call MPI_SEND(sbuf, m, MPI_INTEGER, other, 2, MPI_COMM_WORLD, ierr)
      call MPI_RECV(rbuf, n, MPI_INTEGER, other, 3, MPI_COMM_WORLD, &
                    status, ierr)
    else
      call MPI_RECV(rbuf, n, MPI_INTEGER, other, 2, MPI_COMM_WORLD, &
                    status, ierr)
      call MPI_SEND(sbuf, m, MPI_INTEGER, other, 3, MPI_COMM_WORLD, ierr)
    end if
    if (ierr /= MPI_SUCCESS) call fail('MPI_RECV failed')
    call MPI_GET_COUNT(status, MPI_INTEGER, count, ierr)
    if (status(MPI_SOURCE) /= other .or. status(MPI_TAG) /= 3 - rank .or. &
        count /= m) call fail('wrong status (exchange 2)')
    do i = 1, n
      if ((i <= m .and. rbuf(i) /= val(other, it, n + i)) .or. &
          (i > m .and. rbuf(i) /= -1)) call fail('wrong value (exchange 2)')
    end do

    ! The sum over all ranks, in place.
    do i = 1, n
      rbuf(i) = val(rank, it, 2 * n + i)
    end do
    call MPI_ALLREDUCE(MPI_IN_PLACE, rbuf, n, MPI_INTEGER, MPI_SUM, &
                       MPI_COMM_WORLD, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_ALLREDUCE failed')
    do i = 1, n
      if (rbuf(i) /= val(0, it, 2 * n + i) + val(1, it, 2 * n + i)) &
        call fail('wrong sum')
    end do

    call MPI_BARRIER(MPI_COMM_WORLD, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_BARRIER failed')
    it = it + 1
  end do

  call MPI_BARRIER(MPI_COMM_WORLD, ierr)
  if (rank == 0) then
    write (*, '(a, i0, a)') 'fortran_p2p: PASS (', it, ' iterations)'
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
        write (*, '(a, i0, a, i0)') 'fortran_p2p: rank ', rank, &
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
    write (0, '(a, i0, a, i0, 2a)') 'fortran_p2p: rank ', rank, &
      ': iteration ', it, ': ', msg
    flush (0)
    call MPI_ABORT(MPI_COMM_WORLD, 1, e)
    stop 1  ! MPI_ABORT may return before the job is killed.
  end subroutine fail

end program fortran_p2p
