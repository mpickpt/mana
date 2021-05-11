#pragma once
#ifndef MPI_VIRTUAL_IDS_H
#define MPI_VIRTUAL_IDS_H

#include <mpi.h>
#include <mutex>

#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"

// Convenience macros
#define MpiCommList  dmtcp_mpi::MpiVirtualization
#define MpiCommKeyvalList    dmtcp_mpi::MpiVirtualization
#define MpiGroupList dmtcp_mpi::MpiVirtualization
#define MpiTypeList  dmtcp_mpi::MpiVirtualization
#define MpiOpList    dmtcp_mpi::MpiVirtualization

#define REAL_TO_VIRTUAL_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).virtualToReal(id)
#define ADD_NEW_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).onCreate(id)
#define REMOVE_OLD_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).onRemove(id)
#define UPDATE_COMM_MAP(v, r) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).virtualToReal(id)
#define ADD_NEW_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).onCreate(id)
#define REMOVE_OLD_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).onRemove(id)
#define UPDATE_GROUP_MAP(v, r) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).virtualToReal(id)
#define ADD_NEW_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).onCreate(id)
#define REMOVE_OLD_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).onRemove(id)
#define UPDATE_TYPE_MAP(v, r) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).virtualToReal(id)
#define ADD_NEW_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).onCreate(id)
#define REMOVE_OLD_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).onRemove(id)
#define UPDATE_OP_MAP(v, r) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", MPI_OP_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", MPI_OP_NULL).virtualToReal(id)
#define ADD_NEW_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", MPI_OP_NULL).onCreate(id)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", MPI_OP_NULL).onRemove(id)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  MpiOpList::instance("MpiCommKeyval", MPI_OP_NULL).updateMapping(v, r)

namespace dmtcp_mpi
{

  class MpiVirtualization
  {
    using mutex_t = std::mutex;
    using lock_t  = std::unique_lock<mutex_t>;

    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static MpiVirtualization& instance(const char *name, int nullId)
      {
	// FIXME:
	// dmtcp_mpi::MpiVirtualization::instance("MpiGroup", 1)._vIdTable.printMaps(true)
	// to access _virTableMpiGroup in GDB.
	// We need a cleaner way to access it.
	if (strcmp(name, "MpiOp") == 0) {
	  static MpiVirtualization _virTableMpiOp(name, nullId);
	  return _virTableMpiOp;
	} else if (strcmp(name, "MpiComm") == 0) {
	  static MpiVirtualization _virTableMpiComm(name, nullId);
	  return _virTableMpiComm;
	} else if (strcmp(name, "MpiGroup") == 0) {
	  static MpiVirtualization _virTableMpiGroup(name, nullId);
	  return _virTableMpiGroup;
	} else if (strcmp(name, "MpiType") == 0) {
	  static MpiVirtualization _virTableMpiType(name, nullId);
	  return _virTableMpiType;
	} else if (strcmp(name, "MpiCommKeyval") == 0) {
	  static MpiVirtualization _virTableMpiType(name, nullId);
	  return _virTableMpiType;
	}
	JWARNING(false)(name)(nullId).Text("Unhandled type");
      }

      int virtualToReal(int virt)
      {
        // Don't need to virtualize the null id
        if (virt == _nullId) {
          return virt;
        }
        // FIXME: Use more fine-grained locking (RW lock; C++17 for shared_lock)
        lock_t lock(_mutex);
        return _vIdTable.virtualToReal(virt);
      }

      int realToVirtual(int real)
      {
        // Don't need to virtualize the null id
        if (real == _nullId) {
          return real;
        }
        // FIXME: Use more fine-grained locking (RW lock; C++17 for shared_lock)
        lock_t lock(_mutex);
        return _vIdTable.realToVirtual(real);
      }

      // Adds the given real id to the virtual id table and creates a new
      // corresponding virtual id.
      // Returns the new virtual id on success, null id otherwise
      int onCreate(int real)
      {
        int vId = _nullId;
        // Don't need to virtualize the null id
        if (real == _nullId) {
          return vId;
        }
        lock_t lock(_mutex);
        if (_vIdTable.realIdExists(real)) {
          // JWARNING(false)(real)(_vIdTable.getTypeStr())
          //         (_vIdTable.realToVirtual(real))
          //         .Text("Real id exists. Will overwrite existing mapping");
          vId = _vIdTable.realToVirtual(real);
        }
        if (!_vIdTable.getNewVirtualId(&vId)) {
          JWARNING(false)(real)(_vIdTable.getTypeStr())
                  .Text("Failed to create a new vId");
        } else {
          _vIdTable.updateMapping(vId, real);
        }
        return vId;
      }

      // Removes virtual id from table and returns the real id corresponding
      // to the virtual id; if the virtual id does not exist in the table,
      // returns null id.
      int onRemove(int virt)
      {
        int realId = _nullId;
        // Don't need to virtualize the null id
        if (virt == _nullId) {
          return realId;
        }
        lock_t lock(_mutex);
        if (_vIdTable.virtualIdExists(virt)) {
          realId = _vIdTable.virtualToReal(virt);
          _vIdTable.erase(virt);
        } else {
          JWARNING(false)(virt)(_vIdTable.getTypeStr())
                  .Text("Cannot delete non-existent virtual id");
        }
        return realId;
      }

      // Updates the mapping for the given virtual id to the given real id.
      // Returns virtual id on success, null-id otherwise
      int updateMapping(int virt, int real)
      {
        int vId = _nullId;
        // Don't need to virtualize the null id
        if (virt == _nullId || real == _nullId) {
          return vId;
        }
        lock_t lock(_mutex);
        if (!_vIdTable.virtualIdExists(virt)) {
          JWARNING(false)(virt)(real)(_vIdTable.getTypeStr())
                  (_vIdTable.realToVirtual(real))
                  .Text("Cannot update mapping for a non-existent virt. id");
          return vId;
        }
        _vIdTable.updateMapping(virt, real);
        return vId;
      }

    private:
      // Pvt. constructor
      MpiVirtualization(const char *name, int nullId)
        : _vIdTable(name, 0, 999999),
          _nullId(nullId),
          _mutex()
      {
      }

      // Virtual Ids Table
      dmtcp::VirtualIdTable<int> _vIdTable;
      // Lock on list
      mutex_t _mutex;
      // Default "NULL" value for id
      int _nullId;
  }; // class MpiId

  class VirtualGlobalCommId {
    public:
      int getNewGlobalId(MPI_Comm comm) {
        int gid = 0;
        int worldRank, commSize;
        MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
        MPI_Comm_size(comm, &commSize);
        int rbuf[commSize];
        MPI_Allgather(&worldRank, 1, MPI_INT, rbuf, 1, MPI_INT, comm);
        for (int i = 0; i < commSize; i++) {
          gid ^= hash(rbuf[i]);
        }
        for (
        // while (1) {
        //   if () {
        //     break;
        //   }
        //   gid++;
        // }
        globalIdTable[comm] = gid;
        return gid;
      }
    private:
      VirtualGlobalCommId()
      {
      }

      // from https://stackoverflow.com/questions/664014/
      // what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
      int hash(int i) {
        return i * 2654435761 % ((unsigned long)1 << 32);
      }
      std::map<MPI_Comm, int globalId> globalIdTable;
  }
};  // namespace dmtcp_mpi

#endif // ifndef MPI_VIRTUAL_IDS_H
