#ifndef MPI_RECORD_REPLAY_H
#define MPI_RECORD_REPLAY_H

#include <mpi.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <typeinfo>

#include "jassert.h"
#include "jconvert.h"

#include "lower_half_api.h"

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

#define LOGGING() \
  dmtcp_mpi::MpiRecordReplay::instance().isReplayOn()

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
  using fcb_t   = std::function<int(const MpiRecord&)>;

  struct FncArg
  {
    void *_buf;

    FncArg(const void *buf, size_t len)
      : _buf(JALLOC_HELPER_MALLOC(len))
    {
      if (_buf && buf) {
        memcpy(_buf, buf, len);
      }
    }

    ~FncArg()
    {
      if (!_buf) {
        JALLOC_HELPER_FREE(_buf);
      }
    }

    operator int() const
    {
      return *(int*)_buf;
    }

    operator int*() const
    {
      return (int*)_buf;
    }

    operator MPI_User_function*() const
    {
      return (MPI_User_function*)_buf;
    }
  };

  class MpiRecord
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      MpiRecord(fcb_t cb, MPI_Fncs type, void *fnc)
        : _cb(cb), _type(type), _fnc(fnc)
      {
      }

      ~MpiRecord()
      {
        _args.clear();
      }

      // Base case to stop the recursion
      void addArgs()
      {
      }

      // Handle one complex argument
      void addArgs(const FncArg *arg)
      {
        _args.push_back(*arg);
      }

      // Handle one MPI_User_function argument
      void addArgs(MPI_User_function *arg)
      {
        _args.push_back(FncArg((void*)arg, sizeof(void*)));
      }

      // Handle one simple argument
      template<typename T>
      void addArgs(const T* arg)
      {
        _args.push_back(FncArg(arg, sizeof(T)));
      }

      // Handle list of arguments
      template<typename T, typename... Targs>
      void addArgs(const T* car, const Targs*... cdr)
      {
        addArgs(car);
        addArgs(cdr...);
      }

      // Execute the callback for this record
      int play() const
      {
        return _cb(*this);
      }

      const FncArg& args(int n) const
      {
        return _args[n];
      }

      MPI_Fncs getType() const
      {
        return _type;
      }

      template<typename T>
      T call(T fptr) const
      {
        return T(_fnc);
      }

    private:
      fcb_t _cb;
      MPI_Fncs _type;
      void *_fnc;
      dmtcp::vector<FncArg> _args;
  };

  class MpiRecordReplay
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static MpiRecordReplay& instance()
      {
        static MpiRecordReplay _records;
        return _records;
      }

      template<typename T, typename... Targs>
      MpiRecord* record(fcb_t cb, MPI_Fncs type,
                        const T fPtr, const Targs*... args)
      {
        lock_t lock(_mutex);
        MpiRecord *rec = new MpiRecord(cb, type, (void*)fPtr);
        if (rec) {
          rec->addArgs(args...);
          _records.push_back(rec);
        }
        return rec;
      }

      int replay()
      {
        int rc = MPI_SUCCESS;
        lock_t lock(_mutex);
        _replayOn = true;
        for (auto rec : _records) {
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
        for (auto i : _records) {
          delete i;
        }
        _replayOn = false;
        _records.clear();
      }

      void cleanComms(dmtcp::set<MPI_Comm> &staleComms)
      {
        bool setChanged = false;
        auto isStaleComm =
          [&](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Comm_split):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(3);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_split_type):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(4);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_create):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(2);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_dup):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(1);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_set_errhandler):
              case GENERATE_ENUM(Attr_put):
              case GENERATE_ENUM(Attr_delete):
              {
                MPI_Comm c = rec->args(0);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  return true;
                }
                return false;
              }
              default:
                return false;
            } };
        do {
          auto it = remove_if(_records.begin(), _records.end(), isStaleComm);
          _records.erase(it, _records.end());
        } while (setChanged);
      }

      void clearGroupLogs(MPI_Group group)
      {
        lock_t lock(_mutex);
        dmtcp::set<MPI_Comm> staleComms;
        auto isValidGroup =
          [group, &staleComms](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Group_incl):
              {
                MPI_Group g = rec->args(0);
                return group == g;
              }
              case GENERATE_ENUM(Comm_group):
              {
                MPI_Comm comm = rec->args(0);
                MPI_Group g = rec->args(1);
                return group == g;
              }
              case GENERATE_ENUM(Comm_create):
              {
                MPI_Comm comm = rec->args(0);
                MPI_Group g = rec->args(1);
                MPI_Comm oldcomm = rec->args(2);
                if (group == g) {
                  staleComms.insert(oldcomm); // save this
                  return true;
                }
                return false;
              }
              default:
                return false;
            } };
        auto it = remove_if(_records.begin(), _records.end(), isValidGroup);
        _records.erase(it, _records.end());
        cleanComms(staleComms);
      }

      void clearCommLogs(MPI_Comm comm)
      {
        lock_t lock(_mutex);
        dmtcp::set<MPI_Comm> staleComms;
        auto isValidComm =
          [comm, &staleComms](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Comm_split):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(3);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_split_type):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(4);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_create):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(2);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_dup):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(1);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_set_errhandler):
              case GENERATE_ENUM(Attr_put):
              case GENERATE_ENUM(Attr_delete):
              {
                MPI_Comm c = rec->args(0);
                return staleComms.count(c) > 0;
              }
              default:
                return false;
            } };
        auto it = remove_if(_records.begin(), _records.end(), isValidComm);
        _records.erase(it, _records.end());
        cleanComms(staleComms);
      }

      bool isReplayOn()
      {
        // FIXME: This needs locking. But we can't do this here, otherwise it'll
        // deadlock
        return !_replayOn;
      }

    private:
      // Pvt. constructor
      MpiRecordReplay()
        : _records(),
          _replayOn(false),
          _mutex()
      {
      }

      // Virtual Ids Table
      dmtcp::vector<MpiRecord*> _records;
      // True on restart, false otherwise
      bool _replayOn;
      // Lock on list
      mutex_t _mutex;
  }; // class MpiRecordReplay

  extern int restoreComms(const MpiRecord& );
  extern int restoreGroups(const MpiRecord& );
  extern int restoreTypes(const MpiRecord& );
  extern int restoreCarts(const MpiRecord& );
  extern int restoreOps(const MpiRecord& );
}; // namespace dmtcp_mpi

extern void restoreMpiState();

#endif // ifndef MPI_RECORD_REPLAY_H
