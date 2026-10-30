/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// MPI_Comm_free and MPI_Ibarrier on two communicators, which different ranks
// call in a different order.  MPI permits this order.  C and D are copies of
// MPI_COMM_WORLD.  Even ranks sleep, free C and start an MPI_Ibarrier on D.
// Odd ranks start the barrier, sleep and free C.  A checkpoint can start after
// only the odd ranks started the barrier.  Then the even ranks free C during
// the drain, and the odd ranks must free C too before the checkpoint.

#include "mana_test.h"

int
main(int argc, char **argv)
{
  mt_init(&argc, &argv, "comm_free_order");
  long it;
  for (it = 0; mt_continue(it); it++) {
    MPI_Comm c, d;
    MPI_Request request;
    MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &c));
    MT_MPI(MPI_Comm_dup(MPI_COMM_WORLD, &d));
    if (mt_rank % 2 == 0) {
      usleep(1000);
      MT_MPI(MPI_Comm_free(&c));
      MT_MPI(MPI_Ibarrier(d, &request));
    } else {
      MT_MPI(MPI_Ibarrier(d, &request));
      usleep(2000);
      MT_MPI(MPI_Comm_free(&c));
    }
    MT_MPI(MPI_Wait(&request, MPI_STATUS_IGNORE));
    MT_MPI(MPI_Comm_free(&d));
  }
  mt_finish(it);
  return 0;
}
