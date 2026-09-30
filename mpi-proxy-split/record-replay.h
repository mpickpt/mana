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

#ifndef MPI_RECORD_REPLAY_H
#define MPI_RECORD_REPLAY_H

#include <mpi.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <typeinfo>
#include <unordered_map>

#include "jassert.h"
#include "jconvert.h"

#include "lower-half-api.h"

#undef GENERATE_ENUM
#define GENERATE_ENUM(ENUM)    MPI_Fnc_##ENUM
#undef GENERATE_FNC_PTR
#define GENERATE_FNC_PTR(FNC)  &MPI_##FNC

// Logs the MPI call to the global MPI calls log object (defined by the
// 'MpiRecordReplay' class). 'cb' specifies the callback that will be used
// to replay the MPI call while restoring the MPI state at restart time. 'fnc'
// represents the current MPI call.  'args ...' can be used to provide a
// variable-length list of arguments to be saved. The saved arguments are useful
// while replaying the call later.
#define LOG_CALL(cb, fnc, args...) \
  dmtcp_mpi::MpiRecordReplay::instance().record(cb, GENERATE_ENUM(fnc),  \
                                                GENERATE_FNC_PTR(fnc), args)

#define RESTORE_MPI_STATE() \
  dmtcp_mpi::MpiRecordReplay::instance().replay()

#define CLEAR_LOG() \
  dmtcp_mpi::MpiRecordReplay::instance().reset()

#define CLEAR_GROUP_LOGS(group) \
  dmtcp_mpi::MpiRecordReplay::instance().clearGroupLogs(group)

#define CLEAR_COMM_LOGS(comm) \
  dmtcp_mpi::MpiRecordReplay::instance().clearCommLogs(comm)

#define FREE_TYPE_LOGS(type) \
  dmtcp_mpi::MpiRecordReplay::instance().freeType(type)

// Returns true if we are currently replaying the MPI calls from the saved MPI
// calls log; false, otherwise. Normally, this would be true while restoring
// the MPI state at restart time. All other times, this would return false.
// We cannot use LOGGING since it's used for enabling JTRACE
#define MPI_LOGGING() \
  dmtcp_mpi::MpiRecordReplay::instance().isReplayOn()

// Calls the wrapper function corresponding to the given type 'type'. (The
// 'rec' object contains a pointer to the wrapper function.)
#define FNC_CALL(type, rec)                                                    \
  ({                                                                           \
    __typeof__(GENERATE_FNC_PTR(type))_real_MPI_## type =                      \
                                  rec.call(GENERATE_FNC_PTR(type));            \
    _real_MPI_ ## type;                                                        \
  })

#define CREATE_LOG_BUF(buf, len) dmtcp_mpi::FncArg(buf, len)


namespace dmtcp_mpi
{
  struct FncArg;
  class MpiRecord;
  class MpiRecordReplay;

  using mutex_t = std::mutex;
  using lock_t  = std::unique_lock<mutex_t>;
  using fcb_t   = std::function<int(MpiRecord&)>;
  using mpi_record_vector_iterator_t = dmtcp::vector<MpiRecord*>::iterator;

  enum TYPE {
    TYPE_INT,
    TYPE_INT_PTR,
    TYPE_INT_ARRAY,
    TYPE_VOID_PTR,
    TYPE_VOID_CONST_PTR,
    TYPE_LONG,
    TYPE_MPI_USER_FNC,
  };

  // Restores the MPI requests and returns MPI_SUCCESS on success
  extern int restoreRequests(MpiRecord&);

  // Struct for saving arbitrary function arguments
  struct FncArg
  {
    void *_data;
    size_t _len;
    enum TYPE _type;

    FncArg(const void *data, size_t len, dmtcp_mpi::TYPE type)
      : _data(JALLOC_HELPER_MALLOC(len)), _len(len)
    {
      _type = type;
      if (_data && data) {
        memcpy(_data, data, len);
      }
    }

    // This constructor is only used by CREATE_LOG_BUF
    FncArg(const void *data, size_t len)
      : _data(JALLOC_HELPER_MALLOC(len)), _len(len)
    {
      // Default _type set to TYPE_INT_ARRAY because this constructor is used
      // by CREATE_LOG_BUF in MPI_Cart functions.
      _type = dmtcp_mpi::TYPE_INT_ARRAY;
      if (_data && data) {
        memcpy(_data, data, len);
      }
    }

    // Each FncArg owns its copy of the data, which its destructor frees
    // when the record holding it is deleted.
    FncArg(const FncArg &arg)
      : FncArg(arg._data, arg._len, arg._type)
    {
    }

    FncArg(FncArg &&arg) noexcept
      : _data(arg._data), _len(arg._len), _type(arg._type)
    {
      arg._data = NULL;
    }

    FncArg& operator=(const FncArg &arg) = delete;

    ~FncArg()
    {
      if (_data) {
        JALLOC_HELPER_FREE(_data);
      }
    }

    // On restart, we will use the restore family of functions in
    // record-replay.cpp. A typical line of code there is:
    //   int arg = rec.args(n);
    // rec.args() returns a 'const FncArg&'.
    // The copy constructor for arg will invokes one of these casts
    // from FncArg to the type for arg. So in this example, the code
    // is invoking the 'int cast operator', resulting in:
    //   int arg = *(int*)(((FncArg)rec.args(n))._data);

#if 0
    operator MPI_Request() const
    {
      return *(MPI_Request*)_data;
    }

    operator MPI_Comm() const
    {
      return *(MPI_Comm*)_data;
    }

    operator MPI_Group() const
    {
      return *(MPI_Group*)_data;
    }

    operator MPI_Datatype() const
    {
      return *(MPI_Datatype*)_data;
    }

    operator MPI_Request*() const
    {
      return (MPI_Request*)_data;
    }

    operator MPI_Comm*() const
    {
      return (MPI_Comm*)_data;
    }

    operator MPI_Group*() const
    {
      return (MPI_Group*)_data;
    }

    operator MPI_Datatype*() const
    {
      return (MPI_Datatype*)_data;
    }
#endif

    // Returns an int corresponding to the saved argument
    operator int() const
    {
      return *(int*)_data;
    }

    // Returns an int pointer to saved argument
    // This also handles types like MPI_Datatype* in MPICH,
    // because MPI_Datatype is an integer in this implementation.
    // This doesn't handle MPI_Aint* because MPI_Aint
    // is 8 bytes (long int).
    operator int*() const
    {
      if (_type == dmtcp_mpi::TYPE_INT_ARRAY) {
        return (int*)_data;
      } else if (_type == dmtcp_mpi::TYPE_INT_PTR) {
        return *(int**)_data;
      } else {
        JASSERT(false).Text("Unsupported arg type");
        return NULL;
      }
    }

    // This is used to handle MPI_Aint*, because in MPICH
    // MPI_Aint is 8 bytes (long int).
    operator long*() const
    {
      return (long*)_data;
    }

    // Returns a void pointer to saved argument
    operator void*() const
    {
      return *(void**)_data;
    }

    operator MPI_User_function*() const
    {
      return *(MPI_User_function**)_data;
    }
  };

  template<typename T>
  FncArg FncArgTyped(T data)
  {
    int a;
    int *b;
    void *c;
    void const *d;
    MPI_User_function *e;
    long f;

    if (typeid(data) == typeid(a)) {
      return FncArg(&data, sizeof(data), TYPE_INT);
    }
    if (typeid(data) == typeid(b)) {
      return FncArg(&data, sizeof(data), TYPE_INT_PTR);
    }
    if (typeid(data) == typeid(c)) {
      return FncArg(&data, sizeof(data), TYPE_VOID_PTR);
    }
    if (typeid(data) == typeid(d)) {
      return FncArg(&data, sizeof(data), TYPE_VOID_CONST_PTR);
    }
    if (typeid(data) == typeid(e)) {
      return FncArg(&data, sizeof(data), TYPE_MPI_USER_FNC);
    }
    if (typeid(data) == typeid(f)) {
      return FncArg(&data, sizeof(data), TYPE_MPI_USER_FNC);
    }
    JASSERT(false).Text("Unkown type for FncArg");
    return FncArgTyped(-1);
  }

  // Represent a single call record
  class MpiRecord
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      MpiRecord(fcb_t cb, MPI_Fncs type, void *fnc)
        : _cb(cb), _type(type), _fnc(fnc), _buffer(NULL), _complete(false)
      {
      }

      ~MpiRecord()
      {
        _args.clear();
	free(_buffer);
      }

      // Base case to stop the recursion
      void addArgs()
      {
      }

      // Handle one complex argument
      void addArgs(const FncArg arg)
      {
        _args.push_back(arg);
      }

      // Handle one MPI_User_function argument
      void addArgs(MPI_User_function *arg)
      {
        _args.push_back(FncArgTyped((void*)arg));
      }

      // Handle one simple argument
      template<typename T>
      void addArgs(const T arg)
      {
        _args.push_back(FncArgTyped(arg));
      }

      // Handle list of arguments
      template<typename T, typename... Targs>
      void addArgs(const T car, const Targs... cdr)
      {
        addArgs(car);
        addArgs(cdr...);
      }

      // Execute the restore callback for this record
      int play() const
      {
        return _cb(const_cast<MpiRecord &>(*this));
      }

      // Returns a reference to the 'n'-th function argument object
      const FncArg& args(int n) const
      {
        return _args[n];
      }

      // Returns the enum MPI_Fncs type of the current MPI record object
      MPI_Fncs getType() const
      {
        return _type;
      }

      void setBuf(void *buf)
      {
        _buffer = buf;
      }

      void *getBuf()
      {
        return _buffer;
      }

      void setComplete(bool complete)
      {
        _complete = complete;
      }

      bool getComplete()
      {
        return _complete;
      }

      // Returns a pointer to the wrapper function corresponding to this MPI
      // record object
      template<typename T>
      T call(T fptr) const
      {
        return T(_fnc);
      }

    private:
      fcb_t _cb; // Callback to invoke to replay this MPI call
      MPI_Fncs _type; // enum MPI_Fncs type of this MPI call
      void *_fnc; // Pointer to the wrapper function of this MPI call
      dmtcp::vector<FncArg> _args; // List of argument objects for this MPI call
      void *_buffer; // opaque data saved in the buffer
      bool _complete;
  };

  // Singleton class representing the entire log of MPI calls, useful for
  // saving and restoring the MPI state
  class MpiRecordReplay
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      // Returns the singleton instance of the MpiRecordReplay class
      static MpiRecordReplay& instance()
      {
        static MpiRecordReplay _records;
        return _records;
      }

      // Record/replay addresses two subtle issues in replaying Ireduce/Ibcast.
      // [1] Checkpoint replays the Ibcast saved in the object log. The
      // receiver is receiving the current MPI Ibcast call. The sender advances
      // to the next MPI Ibcast call. Since only the buffer address is saved in
      // the log, the sender's buffer value is different in the MPI next call.
      // That causes the receiver gets wrong buffer.
      // [2] The completed requests is not replayed in the replay. The Ibcast
      // sender request is completed while the receiver is still not finished.
      // In replay, the receiver's request is replay and waits for the broadcast
      // message. However, sender's request is completed so it won't be replayed
      // and won't send message. That causes the receiver waiting forever.
      // The record-replay workflows is below.
      // [1] All requests saved in the record-replay are replayed.
      // [2] Saves Ibcast buffer value in addition to its address.
      // [3] For the completed sender's request, the saved buffer value is copied
      // to temporary buffer before it is replay.
      // [4] For the completed receiver's request, a temporary buffer is created
      // to consume the broadcast message. Since the request is completed, we
      // don't want to impact the original buffer.
      // [5] For the requests that is not completed yet, the original buffer address
      // is used in replay for both the sender and the receiver.
      //
      // Records an MPI call with its arguments in the MPI calls log. Returns
      // a pointer to the inserted MPI record object (containing the details
      // of the call).
      template<typename T, typename... Targs>
      MpiRecord* record(fcb_t cb, MPI_Fncs type,
                        const T fPtr, const Targs... args)
      {
        MpiRecord *rec = new MpiRecord(cb, type, (void*)fPtr);
        if (rec) {
          rec->addArgs(args...);
          switch (type) {
            case GENERATE_ENUM(Type_contiguous):
            case GENERATE_ENUM(Type_create_hvector):
            case GENERATE_ENUM(Type_indexed):
            case GENERATE_ENUM(Type_create_struct):
            case GENERATE_ENUM(Type_dup):
            case GENERATE_ENUM(Type_create_resized):
              datatypeCreated(rec);
              break;
            case GENERATE_ENUM(Type_commit):
              // No reference: freeType() removes the commit records of a
              // type together with the record that created it.
              break;
            case GENERATE_ENUM(Type_free):
              // Not logged; see freeType().
              delete rec;
              return NULL;
            default:
              // The 'default' cases include record types like
              //     comm_create, comm_group, group_incl, etc.
              // Those known types only need to be recorded.  So they don't
              //     have any case label to pre-process their record info
              //     before they are recorded.
              break;
          }
	  {
            lock_t lock(_mutex);
            _records.push_back(rec);
	  }
        }
        return rec;
      }

      // Replays the MPI calls from the log. Returns MPI_SUCCESS on success.
      int replay()
      {
        int rc = MPI_SUCCESS;
        lock_t lock(_mutex);
        _replayOn = true;
        for (MpiRecord* rec : _records) {
          rc = rec->play();
          if (rc != MPI_SUCCESS) {
            break;
          }
        }
        _replayOn = false;
        return rc;
      }

      void reset()
      {
        lock_t lock(_mutex);
        for (MpiRecord* i : _records) {
          delete i;
        }
        _replayOn = false;
        _records.clear();
      }

      // Returns true if we are currently replaying the MPI calls
      bool isReplayOn()
      {
        // FIXME: This needs locking. But we can't do this here, otherwise it'll
        // deadlock
        return !_replayOn;
      }

      void printRecords(bool print);

      // Called by the MPI_Type_free wrapper.  Once no logged datatype was
      // built from 'type' and no pending MPI_Isend/MPI_Irecv uses it,
      // removes the records that created and committed it and releases its
      // virtual id; then does the same for the freed datatypes it was built
      // from.  Without this, a program that creates and frees a datatype
      // for every message (as BLACS does for every broadcast) grows the log
      // and the virtual-id table without bound, which slows it down.
      void freeType(MPI_Datatype type);
    private:
      // Pvt. constructor
      MpiRecordReplay()
        : _records(),
          _replayOn(false),
          _mutex()
      {
      }

      // Registers the datatype that a type-constructor record creates, and
      // adds a reference to each logged datatype it was built from.
      void datatypeCreated(const MpiRecord *rec);

      // Drops a reference to a logged datatype.  When none is left, removes
      // its records, releases its virtual id, and drops the references it
      // held to the datatypes it was built from.  Requires _mutex.
      void releaseType(MPI_Datatype type);

      // Virtual Ids Table
      dmtcp::vector<MpiRecord*> _records;
      // Logged datatypes and their references: one from the application
      // until it calls MPI_Type_free, and one from each logged datatype
      // built from it.
      std::unordered_map<MPI_Datatype, int> _datatypeMap;
      // Datatypes freed while a pending MPI_Isend/MPI_Irecv used them.
      // freeType() releases them once the calls have completed.
      dmtcp::vector<MPI_Datatype> _typesFreedWhilePending;
      // True on restart, false otherwise
      bool _replayOn;
      // Lock on list
      mutex_t _mutex;
  }; // class MpiRecordReplay


  // Restores the MPI types and returns MPI_SUCCESS on success
  extern int restoreTypes(MpiRecord& );

  // Restores the MPI cartesian communicators and returns MPI_SUCCESS on success
  extern int restoreCarts(MpiRecord& );
}; // namespace dmtcp_mpi

// Restores the MPI state by recreating the communicator, groups, types, etc.
// post restart
extern void restoreMpiLogState();
#ifdef SINGLE_CART_REORDER
extern void setCartesianCommunicator(void *getCartesianCommunicatorFptr);
#endif
#endif // ifndef MPI_RECORD_REPLAY_H
