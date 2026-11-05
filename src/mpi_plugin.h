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

#ifndef _MPI_PLUGIN_H
#define _MPI_PLUGIN_H

#include <mpi.h>
#include <cstdint>
#include <cstddef>

#include "dmtcp.h"
#include "lower-half-api.h"

void recordPreMpiInitMaps();
void recordPostMpiInitMaps();
void init_tag_ub();  // mpi-wrappers/mpi_comm_wrappers.cpp
void check_tag_ub();
void init_predefined_attributes();
int copy_comm_attributes(MPI_Comm oldcomm, MPI_Comm newcomm);
int delete_self_attributes();

enum mana_state_t {
  UNKNOWN_STATE,
  RUNNING,
  CKPT_COLLECTIVE,
  CKPT_P2P
};

extern mana_state_t mana_state;
extern bool g_libmana_is_initialized;
extern bool g_libmpi_is_initialized;  // MANA's MPI_Init has returned

#endif // ifndef _MPI_PLUGIN_H
