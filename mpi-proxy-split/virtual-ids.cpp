/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *   Edited 2023 by Leonid Belyaev                                          *
 *   belyaev.l@northeastern.edu                                             *
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
#include <map>

#include <mpi.h>
#include <stdlib.h>

#include "dmtcp.h"


#define CONCAT(a, b) a ## b

// Returns the real type for a virtual type
#define VIRTUAL_TO_REAL(virt_objid) \
  UniversalVirtualIdTable::instance().virtualToReal((UniversalMpiType)virt_objid)

// Returns the virtual type for a real type.
#define REAL_TO_VIRTUAL(real_objid)		\
  UniversalVirtualIdTable::instance().realToVirtual((UniversalMpiType)real_objid)

// I think it necessary to retain seperate creation macros, because each one needs to create a different metadata struct.
// One might be able to simplify these using CONCAT and __typeof__.

// Adds the given real id to the virtual id table, returns virtual id.
#define ADD_NEW_FILE(real_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().onCreateFile(*(long *)virt_objid)

#define ADD_NEW_COMM(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateComm(*(long *)real_objid)

#define ADD_NEW_GROUP(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateGroup(*(long *)real_objid)

#define ADD_NEW_TYPE(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateType(*(long *)real_objid)

#define ADD_NEW_OP(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateOp(*(long *)real_objid)

#define ADD_NEW_COMM_KEYVAL(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateCommKeyval(*(long *)real_objid)

#define ADD_NEW_REQUEST(real_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateRequest(*(long *)real_objid)

// Removes an old vid mapping, returns the mapped real id.
#define REMOVE_OLD(virt_objid) \
  UniversalVirtualIdTable::instance().onRemove((UniversalMpiType)virt_objid)

// Update an existing vid->rid mapping.
#define UPDATE_MAP(virt_objid, real_objid) \
  UniversalVirtualIdTable::instance().updateMapping((UniversalMpiType)virt_objid, (UniversalMpiType)real_objid)

#define MAX_VIRTUAL_ID 999

// --- metadata structs ---

struct virt_comm_t {
    MPI_Comm real_comm; // Real MPI communicator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    unsigned int ggid; // Global Group ID
    unsigned long seq_num; // Sequence number for the CVC algorithm
    unsigned long target; // Target number for the CVC algorithm
    int size; // Size of this communicator
    int local_rank; // local rank number of this communicator
    int *ranks; // list of ranks of the group.
    // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?
};

struct virt_group_t {
    MPI_Group real_group; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
};

struct virt_request_t {
    MPI_Request request; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    enum request_kind; // P2P request or collective request
    MPI_Status status; // Real MPI status in the lower-half
};

struct virt_op_t {
    MPI_Op real_op; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
};

struct virt_datatype_t {
  // TODO add mpi type identifier field virtual class
    MPI_Type real_datatype; // Real MPI type in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    // Components of user-defined datatype.
    MPI_count num_integers;
    int *integers;
    MPI_count num_addresses;
    int *addresses;
    MPI_count num_large_counts;
    int *large_counts;
    MPI_count num_datatypes;
    int *datatypes;
    int *combiner;
};

union virt_t {
    virt_comm_t comm;
    virt_group_t group;
    virt_request_t request;
    virt_op_t op;
    virt_datatype_t datatype;
};

union UniversalMpiType {
  MPI_Comm comm;
  MPI_Group group;
  MPI_Datatype datatype;
  MPI_Op operation;
  MPI_File file;
  MPI_Request request;
  operator MPI_Comm () const { return comm; }
  operator MPI_Group () const { return group; }
  operator MPI_Datatype () const { return datatype; }
  operator MPI_Op () const { return operation; }
  operator MPI_File () const { return file; }
  operator MPI_Request () const { return request; }
};

// MpiVirtualization, but for all types.
// It is the caller's responsibility to cast the `long` ids to their appropriate types.
class UniversalVirtualIdTable
{
  public:

    static UniversalVirtualIdTable& instance()
    {
      static UniversalVirtualIdTable instance;
      return instance;
    }

  UniversalMpiType virtualToReal(UniversalMpiType virt) {
    if (isNull(virt)) {
      return vit;
    }

    return _vIdTable.virtualToReal(virt).real_thing; // vid -> wrapper structures
  }

  UniversalMpiType realToVirtual(UniversalMpiType real) {
    if (isNull(real)) {
      return real;
    }
    return _vIdTable.realToVirtual(real);
  }

  UniversalMpiType onCreate(UniversalMpiType real) {
    UniversalMpiType vId = NULL;

    if (isNull(real)) {
      return real;
    }

    if (_vIdTable.realIdExists(real)) {
      // "Adding an existing real id is a legal operation."
      vId = _vIdTable.realToVirtual(real);
    } else {
      if (_count > _max) {

      }
      UniveralMpiType realWrapper = malloc(sizeof(virt_t));
      virt_t.real_thing = real;
      // vid -> struct 


      if (!_vIdTable.getNewVirtualId(&vId)) { // TODO 
	JWARNING(false)(real)(_vIdTable.getTypeStr())
	  .Text("Failed to create a new vId");
      } else {
	_vIdTable.updateMapping(vId, realWrapper);
      }
    }
    return vId;
  }

  UniversalMpiType onRemove(UniversalMpiType virt) {
    UniversalMpiType realId = _nullId; // TODO how to return a "Null Universal?"

    if (isNull(virt)) {
      return virt;
    }
    // DMTCP virtual id table already does the lock around the table.
    if (_vIdTable.virtualIdExists(virt)) {
	realId = _vIdTable.virtualToReal(virt);
	_vIdTable.erase(virt);
    } else {
	JWARNING(false)(virt)(_vIdTable.getTypeStr())
		.Text("Cannot delete non-existent virtual id");
    }
    return realId;
  }

  UniversalMpiType updateMapping(UniversalMpiType virt, UniversalMpiType real) {
    if (isNull(virt)) {
      return virt;
        }
        // DMTCP virtual id table already does the lock around the table.
        if (!_vIdTable.virtualIdExists(virt)) {
          JWARNING(false)(virt)(real)(_vIdTable.getTypeStr())
                  (_vIdTable.realToVirtual(real))
                  .Text("Cannot update mapping for a non-existent virt. id");
          return _nullId;
        }
        _vIdTable.updateMapping(virt, real);
        return virt;
  }

  // View mpi.h for details on these constants.
  // mpi.h lists "results of the compare operations". Should I use that instead?
  bool isNull(const UniversalMpiType& id) { 
    return id == &MPI_COMM_NULL ||
      id == &MPI_OP_NULL ||
      id == &MPI_GROUP_NULL ||
      id == &MPI_DATATYPE_NULL ||
      id == &MPI_REQUEST_NULL ||
      id == &MPI_ERRHANDLER_NULL ||
      id == &MPI_MESSAGE_NULL ||
      id == &MPI_MESSAGE_NO_PROC
  }

  protected:
    dmtcp::VirtualIdTable<UniversalMpiType> _vIdTable;
  
  private:
  UniversalVirtualIdTable(std::size_t max = MAX_VIRTUAL_ID)
    {
	_vIdTable = _vIdTable("UniversalMPIType", (UniversalMpiType)0, (size_t)999999)
    }
};

// TODO use DMTCP? Find a way to "union" all of the mpi types
// MPI comm, mpi group
// virtual class and inheritance to create universal IdType

// Use salloc to run compile

// SLURM

// salloc -n1, maybe time flag
