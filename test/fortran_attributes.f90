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

! Fortran attributes between two ranks.  At start, MPI_COMM_WORLD gets
! attributes of MPI_COMM_CREATE_KEYVAL keyvals with Fortran callbacks, with
! MPI_COMM_DUP_FN and with MPI_COMM_NULL_COPY_FN, and of MPI_KEYVAL_CREATE
! keyvals with Fortran callbacks and with MPI_DUP_FN.  Each iteration then
! checks, across checkpoints and restarts: MPI_TAG_UB and MPI_APPNUM; that
! MPI_ATTR_GET writes an INTEGER and no more; the attributes that MPI_COMM_DUP
! copies; the delete callbacks of a replaced value, of MPI_COMM_FREE and of
! a freed keyval.  Stops as the C tests do (see mana_test.h); then rank 0
! prints "fortran_attributes: PASS".

module attr_state
  implicit none
  include 'mpif.h'
  integer, parameter :: extra2 = 12345, extra1 = 777
  integer :: rank = 0, it = 0
  integer :: copies = 0, deletes = 0
  integer(kind=MPI_ADDRESS_KIND) :: deleted_sum = 0
contains

  ! MPI_COMM_CREATE_KEYVAL's callbacks: the copy is the value plus one.
  subroutine copy2(oldcomm, keyval, extra_state, val_in, val_out, flag, ierr)
    integer :: oldcomm, keyval, ierr
    integer(kind=MPI_ADDRESS_KIND) :: extra_state, val_in, val_out
    logical :: flag
    if (extra_state /= extra2) call fail('copy2: wrong extra state')
    val_out = val_in + 1
    flag = .true.
    copies = copies + 1
    ierr = MPI_SUCCESS
  end subroutine copy2

  subroutine delete2(comm, keyval, val, extra_state, ierr)
    integer :: comm, keyval, ierr
    integer(kind=MPI_ADDRESS_KIND) :: val, extra_state
    if (extra_state /= extra2) call fail('delete2: wrong extra state')
    deletes = deletes + 1
    deleted_sum = deleted_sum + val
    ierr = MPI_SUCCESS
  end subroutine delete2

  ! MPI_KEYVAL_CREATE's callbacks, with INTEGER arguments.
  subroutine copy1(oldcomm, keyval, extra_state, val_in, val_out, flag, ierr)
    integer :: oldcomm, keyval, extra_state, val_in, val_out, ierr
    logical :: flag
    if (extra_state /= extra1) call fail('copy1: wrong extra state')
    val_out = val_in + 1
    flag = .true.
    copies = copies + 1
    ierr = MPI_SUCCESS
  end subroutine copy1

  subroutine delete1(comm, keyval, val, extra_state, ierr)
    integer :: comm, keyval, val, extra_state, ierr
    if (extra_state /= extra1) call fail('delete1: wrong extra state')
    deletes = deletes + 1
    deleted_sum = deleted_sum + val
    ierr = MPI_SUCCESS
  end subroutine delete1

  subroutine fail(msg)
    character(len=*), intent(in) :: msg
    integer :: e
    write (0, '(a, i0, a, i0, 2a)') 'fortran_attributes: rank ', rank, &
      ': iteration ', it, ': ', msg
    flush (0)
    call MPI_ABORT(MPI_COMM_WORLD, 1, e)
    stop 1  ! MPI_ABORT may return before the job is killed.
  end subroutine fail

end module attr_state

program fortran_attributes
  use iso_c_binding, only: c_int
  use attr_state
  implicit none

  interface
    integer(c_int) function usleep(usec) bind(c, name='usleep')
      import :: c_int
      integer(c_int), value :: usec
    end function usleep
  end interface

  integer :: ierr, nprocs, i, r, iterations
  integer :: k_copy, k_dup, k_null, k1, k1dup, k_tmp, k_saved
  integer :: buf(2)  ! buf(2) guards against a write past an INTEGER
  ! One EXTRA_STATE per keyval: MPICH passes the variable itself to the
  ! callbacks.
  integer(kind=MPI_ADDRESS_KIND) :: extra_copy = extra2, extra_tmp = extra2
  integer(kind=MPI_ADDRESS_KIND) :: extra_none = 0
  integer :: extra_k1 = extra1, extra_k1dup = 0
  integer(kind=MPI_ADDRESS_KIND) :: val, tag_ub, appnum
  integer(kind=MPI_ADDRESS_KIND) :: sum_before, want
  integer :: deletes_before, copies_before, dup
  logical :: flag, appnum_flag  ! MPI_APPNUM exists if the launcher sets it
  character(len=32) :: arg
  character(len=4096) :: control
  integer(8) :: last = -1

  call MPI_INIT(ierr)
  call MPI_COMM_RANK(MPI_COMM_WORLD, rank, ierr)
  call MPI_COMM_SIZE(MPI_COMM_WORLD, nprocs, ierr)
  if (nprocs /= 2) call fail('needs 2 ranks')

  iterations = -1
  do i = 1, command_argument_count() - 1
    call get_command_argument(i, arg)
    if (arg == '-n') then
      call get_command_argument(i + 1, arg)
      read (arg, *) iterations
    end if
  end do
  call get_environment_variable('MT_CONTROL', control)

  call MPI_COMM_GET_ATTR(MPI_COMM_WORLD, MPI_TAG_UB, tag_ub, flag, ierr)
  if (ierr /= MPI_SUCCESS .or. .not. flag .or. tag_ub < 32767) &
    call fail('MPI_TAG_UB')
  call MPI_COMM_GET_ATTR(MPI_COMM_WORLD, MPI_APPNUM, appnum, appnum_flag, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_APPNUM')

  call MPI_COMM_CREATE_KEYVAL(copy2, delete2, k_copy, extra_copy, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_CREATE_KEYVAL')
  call MPI_COMM_CREATE_KEYVAL(MPI_COMM_DUP_FN, MPI_COMM_NULL_DELETE_FN, &
                              k_dup, extra_none, ierr)
  call MPI_COMM_CREATE_KEYVAL(MPI_COMM_NULL_COPY_FN, MPI_COMM_NULL_DELETE_FN, &
                              k_null, extra_none, ierr)
  call MPI_KEYVAL_CREATE(copy1, delete1, k1, extra_k1, ierr)
  if (ierr /= MPI_SUCCESS) call fail('MPI_KEYVAL_CREATE')
  call MPI_KEYVAL_CREATE(MPI_DUP_FN, MPI_NULL_DELETE_FN, k1dup, extra_k1dup, &
                         ierr)
  val = 1000 + rank
  call MPI_COMM_SET_ATTR(MPI_COMM_WORLD, k_copy, val, ierr)
  val = 2000 + rank
  call MPI_COMM_SET_ATTR(MPI_COMM_WORLD, k_dup, val, ierr)
  val = 3000 + rank
  call MPI_COMM_SET_ATTR(MPI_COMM_WORLD, k_null, val, ierr)
  call MPI_ATTR_PUT(MPI_COMM_WORLD, k1, 4000 + rank, ierr)
  call MPI_ATTR_PUT(MPI_COMM_WORLD, k1dup, 5000 + rank, ierr)
  if (ierr /= MPI_SUCCESS) call fail('setting the attributes')

  do while (more())
    r = usleep(1000_c_int)

    ! Predefined attributes keep their launch values.
    call MPI_COMM_GET_ATTR(MPI_COMM_WORLD, MPI_TAG_UB, val, flag, ierr)
    if (.not. flag .or. val /= tag_ub) call fail('MPI_TAG_UB changed')
    call MPI_COMM_GET_ATTR(MPI_COMM_WORLD, MPI_APPNUM, val, flag, ierr)
    if ((flag .neqv. appnum_flag) .or. (flag .and. val /= appnum)) &
      call fail('MPI_APPNUM changed')
    buf = -7
    call MPI_ATTR_GET(MPI_COMM_WORLD, MPI_TAG_UB, buf(1), flag, ierr)
    if (.not. flag .or. buf(1) /= tag_ub .or. buf(2) /= -7) &
      call fail('MPI_ATTR_GET of MPI_TAG_UB')
    buf = -7
    call MPI_ATTR_GET(MPI_COMM_WORLD, k1, buf(1), flag, ierr)
    if (.not. flag .or. buf(1) /= 4000 + rank .or. buf(2) /= -7) &
      call fail('MPI_ATTR_GET')

    ! MPI_COMM_DUP copies what the copy callbacks return.
    copies_before = copies
    call MPI_COMM_DUP(MPI_COMM_WORLD, dup, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_DUP')
    if (copies /= copies_before + 2) call fail('copy callbacks not called')
    call check2(dup, k_copy, 1001_MPI_ADDRESS_KIND + rank, 'k_copy')
    call check2(dup, k_dup, 2000_MPI_ADDRESS_KIND + rank, 'k_dup')
    call MPI_COMM_GET_ATTR(dup, k_null, val, flag, ierr)
    if (flag) call fail('MPI_COMM_NULL_COPY_FN copied')
    call check1(dup, k1, 4001 + rank, 'k1')
    call check1(dup, k1dup, 5000 + rank, 'k1dup')

    ! Replacing a value, freeing the communicator and deleting an attribute
    ! of a freed keyval call the delete callbacks.
    deletes_before = deletes
    sum_before = deleted_sum
    val = it
    call MPI_COMM_SET_ATTR(dup, k_copy, val, ierr)
    call MPI_COMM_DELETE_ATTR(dup, k_dup, ierr)
    call MPI_COMM_FREE(dup, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_FREE')
    call MPI_COMM_CREATE_KEYVAL(MPI_COMM_NULL_COPY_FN, delete2, k_tmp, &
                                extra_tmp, ierr)
    val = 77
    call MPI_COMM_SET_ATTR(MPI_COMM_WORLD, k_tmp, val, ierr)
    k_saved = k_tmp
    call MPI_COMM_FREE_KEYVAL(k_tmp, ierr)
    if (k_tmp /= MPI_KEYVAL_INVALID) call fail('MPI_COMM_FREE_KEYVAL')
    call MPI_COMM_DELETE_ATTR(MPI_COMM_WORLD, k_saved, ierr)
    if (ierr /= MPI_SUCCESS) call fail('MPI_COMM_DELETE_ATTR of a freed keyval')
    want = (1001 + rank) + it + (4001 + rank) + 77
    if (deletes /= deletes_before + 4 .or. deleted_sum - sum_before /= want) &
      call fail('delete callbacks')

    call MPI_BARRIER(MPI_COMM_WORLD, ierr)
    it = it + 1
  end do

  call MPI_BARRIER(MPI_COMM_WORLD, ierr)
  if (rank == 0) then
    write (*, '(a, i0, a)') 'fortran_attributes: PASS (', it, ' iterations)'
    flush (6)
  end if
  call MPI_FINALIZE(ierr)

contains

  subroutine check2(comm, keyval, want, what)
    integer, intent(in) :: comm, keyval
    integer(kind=MPI_ADDRESS_KIND), intent(in) :: want
    character(len=*), intent(in) :: what
    integer(kind=MPI_ADDRESS_KIND) :: v
    logical :: f
    integer :: e
    call MPI_COMM_GET_ATTR(comm, keyval, v, f, e)
    if (e /= MPI_SUCCESS .or. .not. f .or. v /= want) &
      call fail('MPI_COMM_DUP: wrong ' // what)
  end subroutine check2

  subroutine check1(comm, keyval, want, what)
    integer, intent(in) :: comm, keyval, want
    character(len=*), intent(in) :: what
    integer :: v(2), e
    logical :: f
    v = -7
    call MPI_ATTR_GET(comm, keyval, v(1), f, e)
    if (e /= MPI_SUCCESS .or. .not. f .or. v(1) /= want .or. v(2) /= -7) &
      call fail('MPI_COMM_DUP: wrong ' // what)
  end subroutine check1

  ! Like mt_continue() in mana_test.h.
  logical function more()
    integer(8) :: now, rate
    integer :: u, stop_at, ios
    if (len_trim(control) > 0) then
      call system_clock(now, rate)
      if (last < 0 .or. now - last >= rate / 10 .or. now < last) then
        last = now
        write (*, '(a, i0, a, i0)') 'fortran_attributes: rank ', rank, &
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

end program fortran_attributes
