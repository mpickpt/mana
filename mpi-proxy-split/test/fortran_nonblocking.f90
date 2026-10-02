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

! Fortran non-blocking calls between two ranks.  Each iteration: an exchange
! with MPI_IRECV and MPI_ISEND completed by MPI_WAITALL with a statuses
! array, one completed by MPI_WAITALL with MPI_STATUSES_IGNORE (which the MPI
! library must not write to), an MPI_IBARRIER completed by MPI_WAIT, and an
! MPI_ALLREDUCE.  Then the non-blocking collectives with a Fortran binding
! of their own (MPI_IALLREDUCE, MPI_IREDUCE_SCATTER, MPI_ISCAN,
! MPI_IALLTOALL(V), MPI_IALLGATHER(V), MPI_IGATHER(V), MPI_ISCATTER(V)),
! completed by MPI_WAITALL or by one MPI_WAIT each.  One rank waits before
! it sends, enters the barrier or posts the collectives, so that requests
! are pending when a checkpoint comes.  Stops as the C tests do (see
! mana_test.h); then rank 0 prints "fortran_nonblocking: PASS".

program fortran_nonblocking
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
  ! The non-blocking collectives: k words per rank, or 1 to 3 in a v-variant.
  integer, parameter :: k = 16, nops = 11
  integer, parameter :: iallreduce = 1, ireduce_scatter = 2, iscan = 3, &
    ialltoall = 4, ialltoallv = 5, iallgather = 6, iallgatherv = 7, &
    igather = 8, igatherv = 9, iscatter = 10, iscatterv = 11
  integer :: cs(2 * k, nops), cr(2 * k, nops), nreq(nops)
  integer :: sc(0:1, nops), sd(0:1, nops), rc(0:1, nops), rd(0:1, nops)
  integer :: op, j, p, root, from, want
  integer :: ierr, rank, nprocs, other, it, iterations, i, r, m, count
  integer :: sbuf(n), rbuf(n), req(2), breq
  integer :: statuses(MPI_STATUS_SIZE, 2), status(MPI_STATUS_SIZE)
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
  ignore = MPI_STATUSES_IGNORE(:, 1)  ! A wait must not change it.

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
    ! Exchange 1: m words, completed with statuses.
    m = n / 2 + mod(it, n / 2)
    rbuf = -1
    statuses = -1
    call MPI_IRECV(rbuf, n, MPI_INTEGER, other, 1, MPI_COMM_WORLD, req(1), &
                   ierr)
    if (mod(it, 2) == rank) r = usleep(1000_c_int)
    do i = 1, m
      sbuf(i) = val(rank, it, i)
    end do
    call MPI_ISEND(sbuf, m, MPI_INTEGER, other, 1, MPI_COMM_WORLD, req(2), &
                   ierr)
    call MPI_WAITALL(2, req, statuses, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_WAITALL failed (exchange 1)')
    if (any(req /= MPI_REQUEST_NULL)) call fail('requests not freed')
    call MPI_GET_COUNT(statuses(:, 1), MPI_INTEGER, count, ierr)
    if (statuses(MPI_SOURCE, 1) /= other .or. &
        statuses(MPI_TAG, 1) /= 1 .or. count /= m) &
      call fail('wrong status (exchange 1)')
    do i = 1, n
      if ((i <= m .and. rbuf(i) /= val(other, it, i)) .or. &
          (i > m .and. rbuf(i) /= -1)) call fail('wrong value (exchange 1)')
    end do

    ! Exchange 2: the send is posted first; MPI_STATUSES_IGNORE.
    do i = 1, n
      sbuf(i) = val(rank, it, n + i)
    end do
    rbuf = -1
    call MPI_ISEND(sbuf, n, MPI_INTEGER, other, 2, MPI_COMM_WORLD, req(1), &
                   ierr)
    if (mod(it + 1, 2) == rank) r = usleep(1000_c_int)
    call MPI_IRECV(rbuf, n, MPI_INTEGER, other, 2, MPI_COMM_WORLD, req(2), &
                   ierr)
    call MPI_WAITALL(2, req, MPI_STATUSES_IGNORE, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_WAITALL failed (exchange 2)')
    if (any(req /= MPI_REQUEST_NULL)) call fail('requests not freed')
    if (any(MPI_STATUSES_IGNORE(:, 1) /= ignore)) &
      call fail('MPI_WAITALL wrote to MPI_STATUSES_IGNORE')
    do i = 1, n
      if (rbuf(i) /= val(other, it, n + i)) &
        call fail('wrong value (exchange 2)')
    end do

    ! A barrier that one rank enters late.
    call MPI_IBARRIER(MPI_COMM_WORLD, breq, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_IBARRIER failed')
    if (mod(it, 2) == rank) r = usleep(1000_c_int)
    call MPI_WAIT(breq, status, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_WAIT failed')
    if (breq /= MPI_REQUEST_NULL) call fail('barrier request not freed')

    ! The sum over all ranks.
    do i = 1, n
      sbuf(i) = val(rank, it, 2 * n + i)
    end do
    rbuf = -1
    call MPI_ALLREDUCE(sbuf, rbuf, n, MPI_INTEGER, MPI_SUM, MPI_COMM_WORLD, &
                       ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_ALLREDUCE failed')
    do i = 1, n
      if (rbuf(i) /= val(0, it, 2 * n + i) + val(1, it, 2 * n + i)) &
        call fail('wrong sum')
    end do

    ! The non-blocking collectives, posted late by one rank.  MPI reads the
    ! count arrays of a v-variant until it completes.
    root = mod(it, 2)
    do op = 1, nops
      do p = 0, 2 * k - 1
        cs(p + 1, op) = nv(op, rank, p)
      end do
    end do
    cr = -1
    do j = 0, 1
      sc(j, ialltoallv) = vc(rank, j)
      rc(j, ialltoallv) = vc(j, rank)
      rc(j, ireduce_scatter) = vc(j, j)
      rc(j, iallgatherv) = vc(j, j)
      rc(j, igatherv) = vc(j, j)
      sc(j, iscatterv) = vc(j, j)
    end do
    sd(0, :) = 0
    rd(0, :) = 0
    sd(1, :) = sc(0, :)
    rd(1, :) = rc(0, :)
    if (mod(it + 1, 2) == rank) r = usleep(1000_c_int)
    call MPI_IALLREDUCE(cs(:, iallreduce), cr(:, iallreduce), k, &
                        MPI_INTEGER, MPI_SUM, MPI_COMM_WORLD, &
                        nreq(iallreduce), ierr)
    call MPI_IREDUCE_SCATTER(cs(:, ireduce_scatter), &
                             cr(:, ireduce_scatter), &
                             rc(:, ireduce_scatter), MPI_INTEGER, MPI_SUM, &
                             MPI_COMM_WORLD, nreq(ireduce_scatter), ierr)
    call MPI_ISCAN(cs(:, iscan), cr(:, iscan), k, MPI_INTEGER, MPI_SUM, &
                   MPI_COMM_WORLD, nreq(iscan), ierr)
    call MPI_IALLTOALL(cs(:, ialltoall), k, MPI_INTEGER, cr(:, ialltoall), &
                       k, MPI_INTEGER, MPI_COMM_WORLD, nreq(ialltoall), ierr)
    call MPI_IALLTOALLV(cs(:, ialltoallv), sc(:, ialltoallv), &
                        sd(:, ialltoallv), MPI_INTEGER, cr(:, ialltoallv), &
                        rc(:, ialltoallv), rd(:, ialltoallv), MPI_INTEGER, &
                        MPI_COMM_WORLD, nreq(ialltoallv), ierr)
    call MPI_IALLGATHER(cs(:, iallgather), k, MPI_INTEGER, &
                        cr(:, iallgather), k, MPI_INTEGER, MPI_COMM_WORLD, &
                        nreq(iallgather), ierr)
    call MPI_IALLGATHERV(cs(:, iallgatherv), vc(rank, rank), MPI_INTEGER, &
                         cr(:, iallgatherv), rc(:, iallgatherv), &
                         rd(:, iallgatherv), MPI_INTEGER, MPI_COMM_WORLD, &
                         nreq(iallgatherv), ierr)
    call MPI_IGATHER(cs(:, igather), k, MPI_INTEGER, cr(:, igather), k, &
                     MPI_INTEGER, root, MPI_COMM_WORLD, nreq(igather), ierr)
    call MPI_IGATHERV(cs(:, igatherv), vc(rank, rank), MPI_INTEGER, &
                      cr(:, igatherv), rc(:, igatherv), rd(:, igatherv), &
                      MPI_INTEGER, root, MPI_COMM_WORLD, nreq(igatherv), ierr)
    call MPI_ISCATTER(cs(:, iscatter), k, MPI_INTEGER, cr(:, iscatter), k, &
                      MPI_INTEGER, root, MPI_COMM_WORLD, nreq(iscatter), ierr)
    call MPI_ISCATTERV(cs(:, iscatterv), sc(:, iscatterv), &
                       sd(:, iscatterv), MPI_INTEGER, cr(:, iscatterv), &
                       vc(rank, rank), MPI_INTEGER, root, MPI_COMM_WORLD, &
                       nreq(iscatterv), ierr)
    if (mod(it, 2) == 0) then
      call MPI_WAITALL(nops, nreq, MPI_STATUSES_IGNORE, ierr)
      if (ierr /= MPI_SUCCESS) call fail('MPI_WAITALL failed (collectives)')
    else
      do op = 1, nops
        call MPI_WAIT(nreq(op), status, ierr)
        if (ierr /= MPI_SUCCESS) call fail('MPI_WAIT failed (collectives)')
      end do
    end if
    if (any(nreq /= MPI_REQUEST_NULL)) call fail('collective not freed')

    do p = 0, k - 1
      call check(iallreduce, p, nv(iallreduce, 0, p) + nv(iallreduce, 1, p))
      want = nv(iscan, 0, p)
      if (rank == 1) want = want + nv(iscan, 1, p)
      call check(iscan, p, want)
      call check(iscatter, p, nv(iscatter, root, rank * k + p))
      do j = 0, 1
        call check(ialltoall, j * k + p, nv(ialltoall, j, rank * k + p))
        call check(iallgather, j * k + p, nv(iallgather, j, p))
        if (rank == root) call check(igather, j * k + p, nv(igather, j, p))
      end do
    end do
    from = rd(rank, ireduce_scatter)
    do p = 0, vc(rank, rank) - 1
      call check(ireduce_scatter, p, nv(ireduce_scatter, 0, from + p) + &
                 nv(ireduce_scatter, 1, from + p))
      call check(iscatterv, p, nv(iscatterv, root, sd(rank, iscatterv) + p))
    end do
    do j = 0, 1
      from = 0
      if (rank == 1) from = vc(j, 0)
      do p = 0, vc(j, rank) - 1
        call check(ialltoallv, rd(j, ialltoallv) + p, &
                   nv(ialltoallv, j, from + p))
      end do
      do p = 0, vc(j, j) - 1
        call check(iallgatherv, rd(j, iallgatherv) + p, nv(iallgatherv, j, p))
        if (rank == root) &
          call check(igatherv, rd(j, igatherv) + p, nv(igatherv, j, p))
      end do
    end do
    it = it + 1
  end do

  call MPI_BARRIER(MPI_COMM_WORLD, ierr)
  if (rank == 0) then
    write (*, '(a, i0, a)') 'fortran_nonblocking: PASS (', it, ' iterations)'
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
        write (*, '(a, i0, a, i0)') 'fortran_nonblocking: rank ', rank, &
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

  ! The value that rank r sends at position p in collective op.
  integer function nv(op, r, p)
    integer, intent(in) :: op, r, p
    nv = val(r, it, 1000 * op + p)
  end function nv

  ! The words that rank a sends to rank b in a v-variant.
  integer function vc(a, b)
    integer, intent(in) :: a, b
    vc = 1 + mod(a + 2 * b + it, 3)
  end function vc

  ! Word p (from 0) that collective op received must be 'want'.
  subroutine check(op, p, want)
    integer, intent(in) :: op, p, want
    character(len=80) :: msg
    if (cr(p + 1, op) /= want) then
      write (msg, '(a, i0, a, i0, a, i0, a, i0)') 'collective ', op, &
        ' word ', p, ': ', cr(p + 1, op), ', not ', want
      call fail(trim(msg))
    end if
  end subroutine check

  ! Like mt_value() in mana_test.h, kept within 32 bits.
  integer function val(r, it, i)
    integer, intent(in) :: r, it, i
    val = r * 1000003 + mod(it, 100000) * 7919 + i
  end function val

  subroutine fail(msg)
    character(len=*), intent(in) :: msg
    integer :: e
    write (0, '(a, i0, a, i0, 2a)') 'fortran_nonblocking: rank ', rank, &
      ': iteration ', it, ': ', msg
    flush (0)
    call MPI_ABORT(MPI_COMM_WORLD, 1, e)
    stop 1  ! MPI_ABORT may return before the job is killed.
  end subroutine fail

end program fortran_nonblocking
