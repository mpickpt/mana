#ifndef TWO_PHASE_ALGO_H
#define TWO_PHASE_ALGO_H

#include <mpi.h>

#include <condition_variable>
#include <functional>
#include <mutex>

#include "jassert.h"
#include "dmtcpmessagetypes.h"
#include "workerstate.h"

// Convenience macro
#define twoPhaseCommit(comm, fnc) \
        dmtcp_mpi::TwoPhaseAlgo::instance().commit(comm, __FUNCTION__, fnc)

using namespace dmtcp;

typedef enum __phase_t
{
  UNKNOWN = -1,
  IS_READY,
  PHASE_1,
  IN_CS,
  OUT_CS,
  READY_FOR_CKPT,
  PHASE_2,
} phase_t;

typedef enum __query_t
{
  NONE = -1,
  INTENT,
  GET_STATUS,
  FREE_PASS,
  CKPT,
} query_t;

typedef struct __rank_state_t
{
  int rank;
  MPI_Comm comm;
  phase_t st;
} rank_state_t;

namespace dmtcp_mpi
{
  using mutex_t = std::mutex;
  using cv_t = std::condition_variable;
  using lock_t  = std::unique_lock<mutex_t>;
  using lg_t  = std::lock_guard<mutex_t>;

  class TwoPhaseAlgo
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static TwoPhaseAlgo& instance()
      {
        static TwoPhaseAlgo algo;
        return algo;
      }

      void clearCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = false;
      }

      void notifyFreePass(bool forCkpt = false)
      {
        phase_t tmp = getCurrState();
        JASSERT(tmp == PHASE_1 || tmp == PHASE_2 || forCkpt)(tmp)
               .Text("Received free pass in wrong state!");
        lg_t lock(_freePassMutex);
        _freePass = true;
        _freePassCv.notify_one();
      }

      void clearFreePass()
      {
        lock_t lock(_freePassMutex);
        _freePass = false;
      }

      int commit(MPI_Comm , const char* , std::function<int(void)> );
      void preSuspendBarrier(const void *);

    private:
      TwoPhaseAlgo()
        : _currState(IS_READY),
          _ckptPendingMutex(), _phaseMutex(), _freePassMutex(),
          _wrapperMutex(), _ckptMsgMutex(),
          _freePassCv(), _phaseCv(),
          _comm(MPI_COMM_NULL),
          _freePass(false), _inWrapper(false),
          _ckptPending(false), _recvdCkptMsg(false)
      {
      }

      phase_t getCurrState()
      {
        lock_t lock(_phaseMutex);
        return _currState;
      }

      void setCurrState(phase_t st)
      {
        lock_t lock(_phaseMutex);
        _currState = st;
        _phaseCv.notify_one();
      }

      bool isInWrapper()
      {
        return _inWrapper;
      }

      bool isCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        return _ckptPending;
      }

      void setCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = true;
        setCurrentState(WorkerState::PRE_SUSPEND);
      }

      void setRecvdCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        _recvdCkptMsg = true;
      }

      void clearCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        _recvdCkptMsg = false;
      }

      bool recvdCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        return _recvdCkptMsg;
      }

      void stop(MPI_Comm , phase_t );
      bool waitForFreePass(MPI_Comm );
      void waitForCkpt();
      phase_t waitForSafeState();
      phase_t waitForFreePassToTakeEffect(phase_t );
      bool informCoordinatorOfCurrState(const DmtcpMessage& , const void* );
      void wrapperEntry(MPI_Comm );
      void wrapperExit();
      bool inWrapper();

      phase_t _currState;
      mutex_t _ckptPendingMutex;
      mutex_t _phaseMutex;
      mutex_t _freePassMutex;
      mutex_t _wrapperMutex;
      mutex_t _ckptMsgMutex;
      cv_t _freePassCv;
      cv_t _phaseCv;
      MPI_Comm _comm;
      bool _freePass;
      bool _inWrapper;
      bool _ckptPending;
      bool _recvdCkptMsg;
  };
};

// Forces the current process to synchronize with the coordinator in order to
// get to a globally safe state for checkpointing
extern void drainMpiCollectives(const void* );

// Clears the pending checkpoint state for the two-phase checkpointing algo
extern void clearPendingCkpt();

#endif // ifndef TWO_PHASE_ALGO_H
