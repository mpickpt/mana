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

#ifdef SINGLE_CART_REORDER
#include <fcntl.h>
#include "cartesian.h"
#endif

#include <mpi.h>
#include "jassert.h"
#include "jconvert.h"

#define USES_MPI_Fnc_strings
#include "record-replay.h"
#include "virtual_id.h"
#include "p2p_log_replay.h"

using namespace dmtcp_mpi;

static int restoreTypeContiguous(MpiRecord& rec);
static int restoreTypeCommit(MpiRecord& rec);
static int restoreTypeHVector(MpiRecord& rec);
static int restoreTypeIndexed(MpiRecord& rec);
static int restoreTypeHIndexed(MpiRecord& rec);
static int restoreTypeFree(MpiRecord& rec);
static int restoreTypeCreateStruct(MpiRecord& rec);
static int restoreTypeDup(MpiRecord& rec);
static int restoreTypeCreateResized(MpiRecord& rec);

static int restoreCartCreate(MpiRecord& rec);
static int restoreCartMap(MpiRecord& rec);
static int restoreCartShift(MpiRecord& rec);
static int restoreCartSub(MpiRecord& rec);

#ifdef SINGLE_CART_REORDER
void create_cartesian_info_mpi_datatype(MPI_Datatype *cidt);
void load_restart_cartesian_mapping(CartesianProperties *cp,
                                    CartesianInfo *ci,
                                    CartesianInfo restart_mapping[]);
void compare_comm_old_and_cart_cartesian_mapping(
       CartesianProperties *cp,
       CartesianInfo checkpoint_mapping[],
       CartesianInfo restart_mapping[],
       int *comm_old_ranks_order,
       int *comm_cart_ranks_order);
void create_comm_old_communicator(CartesianProperties *cp,
                                  int *comm_old_ranks_order);
void create_comm_cart_communicator(CartesianProperties *cp,
                                   int *comm_cart_ranks_order);
#endif

void
restoreMpiLogState()
{
  JASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS)
          .Text("Failed to restore MPI state");
}

int dmtcp_mpi::restoreTypes(MpiRecord &rec) {
  int rc = -1;
  JTRACE("Restoring MPI derived types");
  switch (rec.getType()) {
    case GENERATE_ENUM(Type_contiguous):
      JTRACE("restoreTypeContiguous");
      rc = restoreTypeContiguous(rec);
      break;
    case GENERATE_ENUM(Type_commit):
      JTRACE("restoreTypeCommit");
      rc = restoreTypeCommit(rec);
      break;
    case GENERATE_ENUM(Type_create_hvector):
      JTRACE("restoreTypeHVector");
      rc = restoreTypeHVector(rec);
      break;
    case GENERATE_ENUM(Type_indexed):
      JTRACE("restoreTypeIndexed");
      rc = restoreTypeIndexed(rec);
      break;
    case GENERATE_ENUM(Type_free):
      JTRACE("restoreTypeFree");
      rc = restoreTypeFree(rec);
      break;
    case GENERATE_ENUM(Type_create_struct):
      JTRACE("restoreTypeCreateStruct");
      rc = restoreTypeCreateStruct(rec);
      break;
    case GENERATE_ENUM(Type_create_hindexed):
      JTRACE("restoreTypeHIndexed");
      rc = restoreTypeHIndexed(rec);
      break;
    case GENERATE_ENUM(Type_dup):
      JTRACE("restoreTypeDup");
      rc = restoreTypeDup(rec);
      break;
    case GENERATE_ENUM(Type_create_resized):
      JTRACE("restoreTypeCreateResized");
      rc = restoreTypeCreateResized(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreCarts(MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI cartesian");
  switch (rec.getType()) {
    case GENERATE_ENUM(Cart_create):
      JTRACE("restoreCartCreate");
      rc = restoreCartCreate(rec);
      break;
    case GENERATE_ENUM(Cart_map):
      JTRACE("restoreCartMap");
      rc = restoreCartMap(rec);
      break;
    case GENERATE_ENUM(Cart_shift):
      JTRACE("restoreCartShift");
      rc = restoreCartShift(rec);
      break;
    case GENERATE_ENUM(Cart_sub):
      JTRACE("restoreCartSub");
      rc = restoreCartSub(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

static int
restoreTypeContiguous(MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(1);
  MPI_Datatype newtype;
  retval = FNC_CALL(Type_contiguous, rec)(count, oldtype, &newtype);
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(2);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

static int
restoreTypeCommit(MpiRecord& rec)
{
  int retval;
  MPI_Datatype type = (MPI_Datatype)(int)rec.args(0);
  retval = FNC_CALL(Type_commit, rec)(&type);
  JWARNING(retval == MPI_SUCCESS)(type).Text("Could not commit MPI datatype");
  return retval;
}

static int
restoreTypeHVector(MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int blocklength = rec.args(1);
  MPI_Aint stride = rec.args(2);
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_create_hvector, rec)(count, blocklength,
                                      stride, oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI hvector datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(4);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

static int
restoreTypeHIndexed(MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int *bs = rec.args(1);
  MPI_Aint *ds = rec.args(2);
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_create_hindexed, rec)(count, bs, ds, oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI hvector datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(4);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

static int
restoreTypeDup(MpiRecord& rec)
{
  int retval;
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(0);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_dup, rec)(oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI hvector datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(1);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

static int
restoreTypeCreateResized(MpiRecord& rec)
{
  int retval;
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(0);
  MPI_Aint lb = rec.args(1);
  MPI_Aint ext = rec.args(2);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_create_resized, rec)(oldtype, lb, ext, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI hvector datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(3);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

void MpiRecordReplay::printRecords(bool print)
{
  JNOTE("Printing _records");
  for(MpiRecord* record : _records) {
    int fnc_idx = record->getType();
    if (print) {
      printf("%s\n", MPI_Fnc_strings[fnc_idx]);
    } else {
      JNOTE("") (MPI_Fnc_strings[fnc_idx]);
    }
  }
}

static int
restoreTypeIndexed(MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int *blocklengths = rec.args(1);
  int *displs = rec.args(2);
  MPI_Datatype oldtype = (MPI_Datatype)(int)rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_indexed, rec)(count, blocklengths,
                                       displs, oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI indexed datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(4);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

static int
restoreTypeFree(MpiRecord& rec)
{
  int retval;
  MPI_Datatype type = (MPI_Datatype)(int)rec.args(0);
  retval = FNC_CALL(Type_free, rec)(&type);
  JWARNING(retval == MPI_SUCCESS)(type).Text("Could not free MPI datatype");
  if (retval == MPI_SUCCESS) {
    // See mpi_type_wrappers.cpp:Type_free
    // NOTE: We cannot remove the old type from the map, since
    // we'll need to replay this call to reconstruct any other type that
    // might have been created using this type.
    //
    // MPI_Datatype realType = REMOVE_OLD_TYPE(type);
  }
  return retval;
}

static int
restoreTypeCreateStruct(MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int *blocklengths = rec.args(1);
  MPI_Aint *displs = rec.args(2);
  MPI_Datatype *types = (MPI_Datatype*)(intptr_t)rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_create_struct, rec)(count, blocklengths,
                                       displs, types, &newtype);
  JWARNING(retval == MPI_SUCCESS)(types)
          .Text("Could not restore MPI struct datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = (MPI_Datatype)(int)rec.args(4);
    update_virt_id((mana_mpi_handle){.datatype = virtType}, (mana_mpi_handle){.datatype = newtype});
  }
  return retval;
}

#ifdef SINGLE_CART_REORDER
int
load_cartesian_properties(const char *filename, CartesianProperties *cp)
{
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  read(fd, &cp->comm_old_size, sizeof(int));
  read(fd, &cp->comm_cart_size, sizeof(int));
  read(fd, &cp->comm_old_rank, sizeof(int));
  read(fd, &cp->comm_cart_rank, sizeof(int));
  read(fd, &cp->reorder, sizeof(int));
  read(fd, &cp->ndims, sizeof(int));
  int array_size = sizeof(int) * cp->ndims;
  read(fd, cp->coordinates, array_size);
  read(fd, cp->dimensions, array_size);
  read(fd, cp->periods, array_size);
  close(fd);
  return 0;
}

void
load_checkpoint_cartesian_mapping(CartesianProperties *cp,
                                  CartesianInfo checkpoint_mapping[])
{
  int ndims = cp->ndims;
  int comm_old_size = cp->comm_old_size;
  for (int i = 0; i < comm_old_size; i++) {
    CartesianProperties cp;
    dmtcp::ostringstream o;
    o << "./ckpt_rank_" << i << "/cartesian.info";
    if (load_cartesian_properties(o.str().c_str(), &cp) == 0) {
      checkpoint_mapping[i].comm_old_rank = cp.comm_old_rank;
      checkpoint_mapping[i].comm_cart_rank = cp.comm_cart_rank;
      for (int j = 0; j < ndims; j++) {
        checkpoint_mapping[i].coordinates[j] = cp.coordinates[j];
      }
    }
  }
}

// Prior to checkpoint we will use the normal variable names, and
// after restart we will use the '_prime' suffix with variable names.
MPI_Comm comm_cart;
MPI_Comm *comm_cart_prime;
MPI_Comm comm_old;
MPI_Comm comm_old_prime;

void
create_cartesian_info_mpi_datatype(MPI_Datatype *cidt)
{
  int retval = -1;
  int lengths[3] = { 1, 1, MAX_CART_PROP_SIZE };

  // Calculate displacements
  // In C, by default padding can be inserted between fields. MPI_Get_address
  // will allow to get the address of each struct field and calculate the
  // corresponding displacement relative to that struct base address. The
  // displacements thus calculated will therefore include padding if any.
  MPI_Aint base_address;
  MPI_Aint displacements[3];
  CartesianInfo dummy_ci;

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);

  retval = NEXT_FUNC(Get_address)(&dummy_ci, &base_address);
  retval += NEXT_FUNC(Get_address)(&dummy_ci.comm_old_rank, &displacements[0]);
  retval += NEXT_FUNC(Get_address)(&dummy_ci.comm_cart_rank, &displacements[1]);
  retval += NEXT_FUNC(Get_address)(&dummy_ci.coordinates[0], &displacements[2]);

  displacements[0] = NEXT_FUNC(Aint_diff)(displacements[0], base_address);
  displacements[1] = NEXT_FUNC(Aint_diff)(displacements[1], base_address);
  displacements[2] = NEXT_FUNC(Aint_diff)(displacements[2], base_address);

  MPI_Datatype types[3] = { MPI_INT, MPI_INT, MPI_INT };

  retval =
    NEXT_FUNC(Type_create_struct)(3, lengths, displacements, types, cidt);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create MPI datatype for <CartesianInfo> struct.");

  retval = NEXT_FUNC(Type_commit)(cidt);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to commit MPI datatype for <CartesianInfo> struct.");

  RETURN_TO_UPPER_HALF();
}

void
load_restart_cartesian_mapping(CartesianProperties *cp, CartesianInfo *ci,
                               CartesianInfo restart_mapping[])
{
  int retval = -1;
  MPI_Datatype ci_type;
  create_cartesian_info_mpi_datatype(&ci_type);
  // Root process will collect the cartesian info and all other process will
  // send their cartesian info
  if (ci->comm_old_rank == 0) {
    retval = NEXT_FUNC(Gather)(ci, 1, ci_type, restart_mapping, 1, ci_type, 0,
                               comm_old_prime);
  } else {
    retval =
      NEXT_FUNC(Gather)(ci, 1, ci_type, NULL, 0, ci_type, 0, comm_old_prime);
  }
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to load restart cartesian mapping.");
}

void
compare_comm_old_and_cart_cartesian_mapping(CartesianProperties *cp,
                                            CartesianInfo checkpoint_mapping[],
                                            CartesianInfo restart_mapping[],
                                            int *comm_old_ranks_order,
                                            int *comm_cart_ranks_order)
{
  for (int i = 0; i < cp->comm_old_size; i++) {
    CartesianInfo *checkpoint = &checkpoint_mapping[i];
    // Iterate through each entry in the <restart_mapping> array and find out
    // the rank of the process whose coordinates are equal to
    // checkpoint.coordinates
    for (int j = 0; j < cp->comm_old_size; j++) {
      CartesianInfo *restart = &restart_mapping[j];
      int sum = 0;
      for (int k = 0; k < cp->ndims; k++) {
        if (checkpoint->coordinates[k] == restart->coordinates[k]) {
          sum += 1;
        }
      }
      if (sum == cp->ndims) {
        comm_old_ranks_order[i] = checkpoint->comm_old_rank;
        comm_cart_ranks_order[i] = checkpoint->comm_cart_rank;
        break;
      }
    }
  }
}

void
create_comm_old_communicator(CartesianProperties *cp, int *comm_old_ranks_order)
{
  int retval = -1;
  MPI_Group comm_old_group_prime, comm_old_group;
  MPI_Comm_group(comm_old_prime, &comm_old_group_prime);
  retval = MPI_Group_incl(comm_old_group_prime, cp->comm_old_size,
                          comm_old_ranks_order, &comm_old_group);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create <comm_old_group> group.");
  retval =
    MPI_Comm_create_group(comm_old_prime, comm_old_group, 121, &comm_old);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create <comm_old> communicator.");
}

void
create_comm_cart_communicator(CartesianProperties *cp, int *comm_cart_ranks_order)
{
  int retval = -1;
  MPI_Group comm_cart_group_prime, comm_cart_group;
  MPI_Comm_group(*comm_cart_prime, &comm_cart_group_prime);

  retval = MPI_Group_incl(comm_cart_group_prime, cp->comm_cart_size,
                          comm_cart_ranks_order, &comm_cart_group);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create <comm_cart_group> group.");
  MPI_Comm comm_cart_tmp;
  retval = MPI_Comm_create_group(*comm_cart_prime, comm_cart_group, 111,
                                 &comm_cart_tmp);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create <comm_cart_tmp> communicator.");
  retval = MPI_Cart_create(comm_cart_tmp, cp->ndims, cp->dimensions,
                           cp->periods, cp->reorder, &comm_cart);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to create <comm_cart> communicator.");
}

void
setCartesianCommunicator(void *getCartesianCommunicatorFptr)
{
  typedef void (*getCartesianCommunicatorFptr_t)(MPI_Comm **);
  ((getCartesianCommunicatorFptr_t)getCartesianCommunicatorFptr)(
    &comm_cart_prime);
}

static int
restoreCartCreate(MpiRecord &rec)
{
  int retval = -1;
  int comm_old_ranks_order[MAX_PROCESSES];
  int comm_cart_ranks_order[MAX_PROCESSES];

  CartesianInfo ci;
  CartesianProperties cp;
  CartesianInfo checkpoint_mapping[MAX_PROCESSES];
  CartesianInfo restart_mapping[MAX_PROCESSES];

  // In current implementation, <comm_old> is MPI_COMM_WORLD
  comm_old_prime = MPI_COMM_WORLD;
  // Get cartesian info of this process
  retval = MPI_Comm_rank(comm_old_prime, &ci.comm_old_rank);
  retval = MPI_Comm_rank(*comm_cart_prime, &ci.comm_cart_rank);
  // Get cartesian properties of this process
  dmtcp::ostringstream o;
  o << "./ckpt_rank_" << ci.comm_old_rank << "/cartesian.info";
  retval = load_cartesian_properties(o.str().c_str(), &cp);
  JASSERT(retval == 0)
  (o.str().c_str()).Text("Failed to load cartesian properties.");
  // Get coordinates of this process
  retval = MPI_Cart_coords(*comm_cart_prime, ci.comm_cart_rank, cp.ndims,
                           ci.coordinates);
  // Load checkpoint cartesian mapping
  load_checkpoint_cartesian_mapping(&cp, checkpoint_mapping);
  // Load restart cartesian mapping
  load_restart_cartesian_mapping(&cp, &ci, restart_mapping);
  retval = MPI_Barrier(MPI_COMM_WORLD);
  JASSERT(retval == MPI_SUCCESS).Text("MPI_Barrier(1) failed.");
  // The root process will populate <comm_old_ranks_order> and
  // <comm_cart_ranks_order> arrays
  if (ci.comm_old_rank == 0) {
    compare_comm_old_and_cart_cartesian_mapping(&cp, checkpoint_mapping,
                                                restart_mapping,
                                                comm_old_ranks_order,
                                                comm_cart_ranks_order);
  }
  retval = MPI_Barrier(MPI_COMM_WORLD);
  JASSERT(retval == MPI_SUCCESS).Text("MPI_Barrier(2) failed.");
  retval = NEXT_FUNC(Bcast)(comm_old_ranks_order, cp.comm_old_size, MPI_INT, 0,
                            comm_old_prime);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to broadcast <comm_old_ranks_order> integer array.");
  retval = MPI_Barrier(MPI_COMM_WORLD);
  JASSERT(retval == MPI_SUCCESS).Text("MPI_Barrier(3) failed.");
  retval = NEXT_FUNC(Bcast)(comm_cart_ranks_order, cp.comm_cart_size, MPI_INT,
                            0, *comm_cart_prime);
  JASSERT(retval == MPI_SUCCESS)
    .Text("Failed to broadcast <comm_cart_ranks_order> integer array.");
  retval = MPI_Barrier(MPI_COMM_WORLD);
  JASSERT(retval == MPI_SUCCESS).Text("MPI_Barrier(4) failed.");
  // Create <comm_old> and <comm_cart> communicators
  create_comm_old_communicator(&cp, comm_old_ranks_order);
  create_comm_cart_communicator(&cp, comm_cart_ranks_order);
  retval = MPI_Barrier(MPI_COMM_WORLD);
  JASSERT(retval == MPI_SUCCESS).Text("MPI_Barrier(5) failed.");
  // Update mapping
  MPI_Comm virtComm = rec.args(5);
  // FIXME: This only works for MPICH but maybe not for other MPI libraries.
  update_virt_id((mana_mpi_handle){.comm = MPI_COMM_WORLD}, (mana_mpi_handle){.comm = comm_old});
  update_virt_id((mana_mpi_handle){.comm = virtComm}, (mana_mpi_handle){.comm = comm_cart});
  return MPI_SUCCESS;
}

#else

static int
restoreCartCreate(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int reorder = rec.args(4);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Cart_create, rec)(comm, ndims, dims,
                                      periods, reorder, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = (MPI_Comm)(int)rec.args(5);
    update_virt_id((mana_mpi_handle){.comm = virtComm}, (mana_mpi_handle){.comm = newcomm});
  }
  return retval;
}

#endif

static int
restoreCartMap(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int newrank = -1;
  retval = FNC_CALL(Cart_map, rec)(comm, ndims, dims, periods, &newrank);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldrank = rec.args(4);
    JASSERT(newrank == oldrank)(oldrank)(newrank).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartShift(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  int direction = rec.args(1);
  int disp = rec.args(2);
  int rank_source = -1;
  int rank_dest = -1;
  retval = FNC_CALL(Cart_shift, rec)(comm, direction,
                                     disp, &rank_source, &rank_dest);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldsrc = rec.args(3);
    int olddest = rec.args(4);
    JASSERT(oldsrc == rank_source && olddest == rank_dest)
           (oldsrc)(olddest)(rank_source)(rank_dest).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartSub(MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = (MPI_Comm)(int)rec.args(0);
  // int ndims = rec.args(1);
  int *remain_dims = rec.args(2);
  MPI_Comm newcomm = MPI_COMM_NULL;
  // LOG_CALL(restoreCarts, Cart_sub, comm, ndims, rs, virtComm);
  retval = FNC_CALL(Cart_sub, rec)(comm, remain_dims, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = (MPI_Comm)(int)rec.args(3);
    update_virt_id((mana_mpi_handle){.comm = virtComm}, (mana_mpi_handle){.comm = newcomm});
  }
  return retval;
}
