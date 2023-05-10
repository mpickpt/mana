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
#include <map>

#include <mpi.h>

#define CONCAT(a, b) a ## b

// Returns the real type for a virtual type
#define VIRTUAL_TO_REAL(virt_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().virtualToReal(*(long *)virt_objid)

// Returns the virtual type for a real type.
#define REAL_TO_VIRTUAL(real_objid)		\
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().realToVirtual(*(long *)virt_objid)

// Adds the given real id to the virtual id table, returns virtual id.
#define ADD_NEW(real_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().onCreate(*(long *)virt_objid)

// Removes an old vid mapping, returns the mapped real id.
#define REMOVE_OLD(virt_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().onRemove(*(long *)virt_objid)

// Update an existing vid->rid mapping.
#define UPDATE_MAP(virt_objid, real_objid) \
  *(__typeof__(&virt_objid))UniversalVirtualIdTable::instance().updateMapping(*(long *)virt_objid)

#define MAX_VIRTUAL_ID 999

// MpiVirtualization, but for all types.
// It is the caller's responsibility to cast the `long` ids to their appropriate types.
class UniversalVirtualIdTable
{
  public:

    static UniversalVirtualIdTable& instance()
    {
      static UniversalVirtualIdTable table();
      return table;
    }

    // TODO: Currently, these just follow the old virtualids from dmtcp.
    // We want these to point to addresses of metadata structures.
    void resetNextVirtualId()
    {
      _nextVirtualId = (_base + 1)
    }

    virt_t* addOneToNextVirtualId()
    {
      virt_t* ret = _nextVirtualId;
      _nextVirtualId = (_nextVirtualId + 1);
      if _nextVirtualId >= (_base + _max) {
          resetNextVirtualId();
      }
      return ret; 
    }

    bool getNewVirtualId(long id) {
      bool res = false;

      // TODO: lock table
      if (_idMapTable.size() < _max) {
        size_t count = 0;
        while (1) {
          virt_t* new_id = addOneToNextVirtualId();
          id_iterator i = _idMapTable.find(new_id);
	  if (i == _virtToRealMap.end()) {
	    *id = reinterpret_cast<long>(new_id);
	    res = true;
	    break;
	  }
	  if (++count == _max) {
	    break;
	  }
        }
      }
      // TODO: unlock table
      return res;
    }

    bool realIdExists(long realId) {
      bool retval = false;

      // TODO lock table
      for (id_iterator i = _virtToRealMap.begin(); i != _virtToRealMap.end(); ++i) {
        if (i->second == id) {
          retval = true;
        }
      }
      // TODO unlock table
      return retval;
    }

    long virtualToReal(long virtualId) {
      return virt_to_real_map[virtualId];
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

    long onCreate(long realId) {
      long virtualId = _nullId;

      if (realId == _nullId) {
        return virtualId;
      }

      if (realIdExists(realId)) {
        // "Adding an existing real id is a legal operation".
        virtualId = realToVirtual(real);
      } else {
        if (!getNewVirtualId(&virtualId)) {
            // TODO: Error out in some fashion
        } else {
            updateMapping(virtualId, realId);
        }
      }
      return virtualId;
    }

    // Removes virtual id from table and returns the corresponding real id. Assuming that it exists, for now.
    long onRemove(long virtualId) {
      long realId = virtualToReal(virtualId);
      _virtToRealMap.erase(virtualId);
      return realId;
    }

    long updateMapping(long virtualId, long realId) {
      _virtToRealMap[virtualId] = realId;
    }

  protected:
    // Casted address of virtual type -> casted address of real type
    std::map<long, long> _virtToRealMap;
    // Next virtualid. This structure is taken from dmtcmp/include/virtualidtable.h
    // TODO: What is a suitable "base" of all the mpi types?
    virt_t* _nextVirtualId;
    // dmtcmp initializes base as a reference to the id type. What would be suitable for all the id types?
    virt_t* _base;
    long _nullId;
    size_t _max;

    // Casted address of virtual type -> casted address of corresponding metadata struct
    std::map<long, long> _virtToMetadataMap;

  private:
    UniversalVirtualIdTable(size_t max = MAX_VIRTUAL_ID)
    {
      _base = 0; // TODO
      _max = max;
      _nullId = 0; // TODO
    }
}


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
}

struct virt_group_t {
    MPI_Group real_group; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
}

struct virt_request_t {
    MPI_Request request; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    enum request_kind; // P2P request or collective request
    MPI_Status status; // Real MPI status in the lower-half
}

struct virt_op_t {
    MPI_Op real_op; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
}

struct virt_datatype_t {
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
}

union virt_t {
    virt_comm_t comm;
    virt_group_t group;
    virt_request_t request;
    virt_op_t op;
    virt_datatype_t datatype;
}
