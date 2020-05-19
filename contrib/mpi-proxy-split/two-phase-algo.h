#ifndef TWO_PHASE_ALGO_H
#define TWO_PHASE_ALGO_H

#include <mpi.h>

#include <condition_variable>
#include <functional>
#include <mutex>

#include "jassert.h"
#include "dmtcpmessagetypes.h"
#include "workerstate.h"
#include "mana_coord_proto.h"

// Convenience macro
#define twoPhaseCommit(comm, fnc) \
        dmtcp_mpi::TwoPhaseAlgo::instance().commit(comm, __FUNCTION__, fnc)

using namespace dmtcp;

namespace dmtcp_mpi
{
  using mutex_t = std::mutex;
  using cv_t = std::condition_variable;
  using lock_t  = std::unique_lock<mutex_t>;
  using lg_t  = std::lock_guard<mutex_t>;

  // This class encapsulates the two-phase MPI collecitve algorithm and the
  // corresponding checkpointing protocol for MANA
  class TwoPhaseAlgo
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      // Returns the singleton instance of the class
      static TwoPhaseAlgo& instance()
      {
        static TwoPhaseAlgo algo;
        return algo;
      }

      // Sets '_ckptPending' to false, indicating that the checkpointing
      // finished successfully
      void clearCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = false;
      }

      // Sets '_freePass' to true and unblocks any threads blocked on the
      // '_freePassCv' condition variable
      // (Executed by the checkpoint thread)
      void notifyFreePass(bool forCkpt = false)
      {
        phase_t tmp = getCurrState();
        JASSERT(tmp == PHASE_1 || tmp == PHASE_2 || forCkpt)(tmp)
               .Text("Received free pass in wrong state!");
        lg_t lock(_freePassMutex);
        _freePass = true;
        _freePassCv.notify_one();
      }

      // Sets '_freePass' to false
      void clearFreePass()
      {
        lock_t lock(_freePassMutex);
        _freePass = false;
      }

      // The main function of the two-phase protocol for MPI collectives
      int commit(MPI_Comm , const char* , std::function<int(void)> );

      // Implements the pre-suspend checkpointing protocol for coordination
      // between DMTCP coordinator and peers
      void preSuspendBarrier(const void *);

    private:

      // Private constructor
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

      // Returns the current checkpointing state: '_currState' of the rank
      phase_t getCurrState()
      {
        lock_t lock(_phaseMutex);
        return _currState;
      }

      // Sets the current checkpointing state: '_currState' of the rank to the
      // given state 'st'
      void setCurrState(phase_t st)
      {
        lock_t lock(_phaseMutex);
        _currState = st;
        _phaseCv.notify_one();
      }

      // Returns true if we are currently executing in an MPI collective wrapper
      // function
      bool isInWrapper()
      {
        return _inWrapper;
      }

      // Returns true if a checkpoint intent message was received from the
      // coordinator and we haven't yet finished checkpointing
      bool isCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        return _ckptPending;
      }

      // Sets '_ckptPending' to true to indicate that a checkpoint intent
      // message was received from the the coordinator
      void setCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = true;
        setCurrentState(WorkerState::PRE_SUSPEND);
      }

      // Sets '_recvdCkptMsg' to true
      void setRecvdCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        _recvdCkptMsg = true;
      }

      // Sets '_recvdCkptMsg' to false
      void clearCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        _recvdCkptMsg = false;
      }

      // Returns the value of '_recvdCkptMsg'
      bool recvdCkptMsg()
      {
        lock_t lock(_ckptMsgMutex);
        return _recvdCkptMsg;
      }

      // Stopping point before entering and after exiting the actual MPI
      // collective call to avoid domino effect and provide bounds on
      // checkpointing time. 'comm' indicates the MPI communicator used
      // for the collective call, and 'p' is the current phase.
      void stop(MPI_Comm , phase_t );

      // Blocks until a free pass message is received from the coordinator
      bool waitForFreePass(MPI_Comm );

      // Blocks and wait for checkpointing to complete
      void waitForCkpt();

      // This is used to ensure that the caller waits for the following 3
      // transitions:
      //   IS_READY -> PHASE_1
      //   OUT_CS   -> PHASE_2
      //   PHASE_1  -> IN_CS
      // Returns the new state of the process.
      phase_t waitForSafeState();

      // Wait for the following 5 transitions:
      //   PHASE_1  -> READY_FOR_CKPT
      //   PHASE_1  -> IN_CS
      //   PHASE_2  -> READY_FOR_CKPT
      //   PHASE_2  -> IS_READY
      //   IS_READY -> IS_READY
      //   PHASE_2  -> READY_FOR_CKPT -> PHASE_1 (If the ckpt thread was too slow.)
      //   PHASE_1  -> IN_CS -> PHASE_2 (If the ckpt thread was too slow.)
      // Returns the new state of the process.
      phase_t waitForFreePassToTakeEffect(phase_t );

      // Sends the given message 'msg' (along with the given 'extraData') to
      // the coordinator
      bool informCoordinatorOfCurrState(const DmtcpMessage& , const void* );

      // Sets '_inWrapper' to true and sets '_comm' to the given 'comm'
      void wrapperEntry(MPI_Comm );

      // Sets '_inWrapper' to false
      void wrapperExit();

      // Returns true if we are executing in an MPI collective wrapper function
      bool inWrapper();

      // Checkpointing state of the current process (MPI rank)
      phase_t _currState;

      // Lock to protect accesses to '_ckptPending'
      mutex_t _ckptPendingMutex;

      // Lock to protect accesses to '_currState'
      mutex_t _phaseMutex;

      // Lock to protect accesses to '_freePass'
      mutex_t _freePassMutex;

      // Lock to protect accesses to '_inWrapper'
      mutex_t _wrapperMutex;

      // Lock to protect accesses to '_recvdCkptMsg'
      mutex_t _ckptMsgMutex;

      // Condition variable to wait-signal based on the state of '_freePass'
      cv_t _freePassCv;

      // Condition variable to wait-signal based on the state of '_currState'
      cv_t _phaseCv;

      // MPI communicator corresponding to the current MPI collective call
      MPI_Comm _comm;

      // True if a free-pass message was received from the coordinator
      bool _freePass;

      // True if we have entered an MPI collective wrapper function
      // TODO: Use C++ atomics
      bool _inWrapper;

      // True if a checkpoint intent message was received from the coordinator
      // and we haven't yet finished checkpointing
      // TODO: Use C++ atomics
      bool _ckptPending;

      // True if a final ready-for-checkpointing message was received from the
      // coordinator, indicating that we have reached a safe state globally
      // TODO: Use C++ atomics
      bool _recvdCkptMsg;
  };
};

// Forces the current process to synchronize with the coordinator in order to
// get to a globally safe state for checkpointing
extern void drainMpiCollectives(const void* );

// Clears the pending checkpoint state for the two-phase checkpointing algo
extern void clearPendingCkpt();

#endif // ifndef TWO_PHASE_ALGO_H
