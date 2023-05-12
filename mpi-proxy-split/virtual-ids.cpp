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


#define CONCAT(a, b) a ## b

// Returns the real type for a virtual type
#define VIRTUAL_TO_REAL(virt_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().virtualToReal(*(long *)virt_objid)

// Returns the virtual type for a real type.
#define REAL_TO_VIRTUAL(real_objid)		\
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().realToVirtual(*(long *)virt_objid)

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

#define ADD_NEW_REQUEST(reak_objid) \
  *(__typeof__(&real_objid))UniversalVirtualIdTable::instance().onCreateRequest(*(long *)real_objid)

// Removes an old vid mapping, returns the mapped real id.
#define REMOVE_OLD(virt_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().onRemove(*(long *)virt_objid)

// Update an existing vid->rid mapping.
#define UPDATE_MAP(virt_objid, real_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().updateMapping(*(long *)virt_objid)

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

    bool realIdExists(long realId) {
      bool retval = false;

      // TODO lock table
      for (id_iterator i = _virtToRealMap.begin(); i != _virtToRealMap.end(); ++i) {
        if (i->second == realId) {
          retval = true;
        }
      }
      // TODO unlock table
      return retval;
    }

    long virtualToReal(long virtualId) {
      return _virtToRealMap[virtualId];
    }

    long realToVirtual(long realId) {
      // TODO: Lock table

      for (id_iterator i = _virtToRealMap.begin(); i!= _virtToRealMap.end(); ++i) {
        if (realId == i->second) {
          // TODO unlock table
          return i->first;
        }
      }
      // TODO unlock table

      return realId;
    }

  // TODO all of these metadata structures need to be filled out. I don't understand the details of the respective algorithms yet, so I do not do it here.
  long onCreateComm(long realId) {
    void* metadata = malloc(sizeof(virt_comm_t));
    onCreate(realId, metadata);
  }

  long onCreateGroup(long realId) {
    void* metadata = malloc(sizeof(virt_group_t));
    onCreate(realId, metadata);
  }

  long onCreateType(long realId) {
    void* metadata = malloc(sizeof(virt_datatype_t));
    onCreate(realId, metadata);
  }

  long onCreateOp(long realId) {
    void* metadata = malloc(sizeof(virt_op_t));
    onCreate(realId, metadata);
  }

  // TODO Comm keyval store
  long onCreateCommKeyval(long realId) {
    void* metadata;
    onCreate(realId, (void *)metadata);
  }

  // TODO
  long onCreateFile(long realId) {
    void* metadata;
    onCreate(realId, (void *)metadata);
  }

  long onCreateRequest(long realId) {
    void* metadata = malloc(sizeof(virt_request_t));
    onCreate(realId, metadata);
  }

  long onCreate(long realId, void* metadata) {
      long virtualId = _nullId;

      if (realId == _nullId) {
        return virtualId;
      }

      if (realIdExists(realId)) {
        // "Adding an existing real id is a legal operation".
        virtualId = realToVirtual(realId);
      } else {
        if (_count > _max) {
            // TODO: Error out in some fashion
        } else {
	  _count++;
	  virtualId = reinterpret_cast<long>(metadata);
          updateMapping(virtualId, realId);
        }
      }
      return virtualId;
    }

    // Removes virtual id from table and returns the corresponding real id. Assuming that it exists, for now.
    long onRemove(long virtualId) {
      void* metadata = reinterpret_cast<void*>(virtualId);
      long realId = virtualToReal(virtualId);

      _virtToRealMap.erase(virtualId);
      free(metadata);
      _count--;

      return realId;
    }

    long updateMapping(long virtualId, long realId) {
      _virtToRealMap[virtualId] = realId;
    }

  protected:
    typedef typename std::map<long, long>::iterator id_iterator;
    // Casted address of virtual type -> casted address of real type
    std::map<long, long> _virtToRealMap;
    long _nullId;
  std::size_t _count;
    std::size_t _max;
  void* _metadataArray; // TODO Calling `malloc()` for every vid creation is suboptimal. An optimal design would probably use this field or one like it, allocating all the memory at once. I don't move forward with this design for now because I'm not sure how to canonically handle the resulting fragmentation issues.

  private:
  UniversalVirtualIdTable(std::size_t max = MAX_VIRTUAL_ID)
    {
      _count = 0;
      _max = MAX_VIRTUAL_ID;
	_nullId = NULL; // TODO include all of respective null pointer
      _metadataArray = malloc(_max * sizeof(virt_t));
    }
};

// TODO use DMTCP? Find a way to "union" all of the mpi types
// MPI comm, mpi group
// virtual class and inheritance to create universal IdType

// Use salloc to run compile

// SLURM

// salloc -n1, maybe time flag
