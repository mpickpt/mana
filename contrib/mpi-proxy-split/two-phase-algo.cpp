#include <ucontext.h>
#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "siginfo.h"
#if 0
#include "split-process.h"
#else

#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H

// Helper class to save and restore context (in particular, the FS register),
// when switching between the upper half and the lower half. In the current
// design, the caller would generally be the upper half, trying to jump into
// the lower half. An example would be calling a real function defined in the
// lower half from a function wrapper defined in the upper half.
// Example usage:
//   int function_wrapper()
//   {
//     SwitchContext ctx;
//     return _real_function();
//   }
// The idea is to leverage the C++ language semantics to help us automatically
// restore the context when the object goes out of scope.
class SwitchContext
{
  private:
    unsigned long upperHalfFs; // The base value of the FS register of the upper half thread
    unsigned long lowerHalfFs; // The base value of the FS register of the lower half

  public:
    // Saves the current FS register value to 'upperHalfFs' and then
    // changes the value of the FS register to the given 'lowerHalfFs'
    explicit SwitchContext(unsigned long );

    // Restores the FS register value to 'upperHalfFs'
    ~SwitchContext();
};

// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs) \
  do { \
    SwitchContext ctx((unsigned long)lhFs)

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF() \
  } while (0)

// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef _SPLIT_PROCESS_H
#endif



using namespace dmtcp_mpi;

void
drainMpiCollectives(const void *data)
{
  TwoPhaseAlgo::instance().preSuspendBarrier(data);
}

void
clearPendingCkpt()
{
  TwoPhaseAlgo::instance().clearCkptPending();
}

void
resetTwoPhaseState()
{
  TwoPhaseAlgo::instance().resetStateAfterCkpt();
}

unsigned long upperHalfFs;

// There can be multiple communicators, each with their own N (N = comm. size).
//
// The coordinator can initiate the checkpoint when none of the communicators
// are "blocked".
//
// General Properties:
// 0. For each collective call, we need to communicate the size of the comm
//    with the coordinator. The coordinator will use this information to
//    figure out the status of the ranks in the set.
// 1. If any rank in a communicator, executes a collective call, this implies
//    that all ranks in the communicator must eventually execute the collective
//    call.
//
// Properties of Phase-1:
// 2. If a rank has entered the trivial barrier, then it's safe to abort
//    the trivial barrier and restart the call later.
// 3. If a rank has exited the trivial barrier, then it's safe to wait until
//    all ranks exit the trivial barrier. (Conclusion: The wait won't be too
//    long, since no rank is blocked inside the barrier.)
// 4. INVARIANT: Each rank will checkpoint either before the trivial barrier,
//    or after the trivial barrier (but before the collective call).
//    REASONING: In the former case, it's safe to checkpoint, since we'll
//    restart the trivial barrier for all the ranks. In the latter case, it's
//    safe to checkpoint, since all of the ranks have exited the trivial
//    barrier and are yet to enter the collective call.
//
// Properties of Phase-2:
// 5. If all of the ranks of a communicator are inside stop(PHASE_2), then those
//    ranks are not preventing a checkpoint. The ranks must then wait for a
//    checkpoint message from the coordinator. On restart, they can just
//    continue (resume) to execute the collective call.
// 6. If some ranks of a communicator are in stop(PHASE_2) and all others are
//    in the collective call, then it's not safe to checkpoint. This can happen,
//    for example, if the other ranks received the checkpoint msg too late.
//    Since at least one rank has entered the collective call, there are
//    are three implications:
//      (a) All ranks of the comm. must have entered phase-2. (In other words,
//          we are guaranteed that there are no stragglers.);
//      (b) Coordinator must allow the ranks of the comm. in stop(PHASE_2) to
//          enter the collective call by giving them a free pass; and
//      (c) We must then wait for all of ranks of the comm. to exit the
//          collective call.
// 7. For simplicity, we assume that if one rank has exited the collective
//    call, then it's safe to wait until all ranks exit the collective call.
//    In particular, we don't have to worry about stragglers, since we know
//    that all ranks (of the communicator) must have entered into the
//    collective call.

using namespace dmtcp_mpi;

int
TwoPhaseAlgo::commit(MPI_Comm comm, const char *collectiveFnc,
                     std::function<int(void)>doRealCollectiveComm)
{
  if (comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }
 
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  wrapperEntry(comm);
  setCurrState(IN_TRIVIAL_BARRIER);
  _commAndStateMutex.unlock();

  addCommHistory(comm);
  JTRACE("Invoking 2PC for")(collectiveFnc);

  getcontext(&beforeTrivialBarrier);

  // Call the trivial barrier
  if (true /*isCkptPending() && inCommHistory(comm)*/) {
    inTrivialBarrierOrPhase1 = true;
    // Set state again incase we returned from beforeTrivialBarrier
    setCurrState(IN_TRIVIAL_BARRIER);
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Request request;
    int flag = 0;
    DMTCP_PLUGIN_DISABLE_CKPT();
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Ibarrier)(realComm, &request);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    while (!flag) {
      // Different MPI implementations have different rules for MPI_Test
      // and MPI_REQUEST_NULL. For OpenMPI, it's allowed to call MPI_TEST
      // with MPI_REQUEST_NULL, and the flag will always be true. But for
      // MPICH, it will cause a fatal error that the request is invalid.
      // So we check the value of request before doing the MPI_Test. If
      // it's MPI_REQUEST_NULL, then we break. This will work with both
      // rules.
      if (request == MPI_REQUEST_NULL) {
        JTRACE("Trivial barrier request is null")(request)(flag)(realComm);
        break;
      }
      DMTCP_PLUGIN_DISABLE_CKPT();
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      JASSERT(request != MPI_REQUEST_NULL)(request)(flag)(realComm);
      int rc = NEXT_FUNC(Test)(&request, &flag, MPI_STATUS_IGNORE);
#ifdef DEBUG
      JASSERT(rc == MPI_SUCCESS)(rc)(realComm)(request)(comm);
#endif
      RETURN_TO_UPPER_HALF();
      DMTCP_PLUGIN_ENABLE_CKPT();
      // FIXME: make this smaller
      struct timespec test_interval = {.tv_sec = 0, .tv_nsec = 100000};
      nanosleep(&test_interval, NULL);
    }
  }

  entering_phase1 = true;
  if (isCkptPending()) {
    stop(comm);
  }
  inTrivialBarrierOrPhase1 = false;
  entering_phase1 = false;
  setCurrState(IN_CS);
  int retval = doRealCollectiveComm();
  // INVARIANT: All of our peers have executed the real collective comm.
  // Now, we can re-enable checkpointing
  // setCurrState(PHASE_2);
  // if (isCkptPending()) {
  //   stop(comm, PHASE_2);
  // }
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  setCurrState(IS_READY);
  wrapperExit();
  _commAndStateMutex.unlock();
  return retval;
}

void
TwoPhaseAlgo::commit_begin(MPI_Comm comm)
{
  wrapperEntry(comm);

#if 0
  getcontext(&beforeTrivialBarrier);
  // inTrivialBarrier should be false, unless we are jumping from
  // the setcontext in threadlist.cpp:stopthisthread because the
  // checkpoint signal was received from the trivial barrier.
  if (inTrivialBarrier) {
    inTrivialBarrier = false;
    raise(CKPT_SIGNAL); // resend the checkpoint signal to this thread
  }

#endif

  // Call the trivial barrier if it's the second time this
  // communicator enters a wrapper after get a free pass
  // from PHASE_2
  if (true /*isCkptPending() && inCommHistory(comm)*/) {
    inTrivialBarrierOrPhase1 = true;
    setCurrState(IN_TRIVIAL_BARRIER);
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    // JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Barrier)(realComm);
    // RETURN_TO_UPPER_HALF();
    inTrivialBarrierOrPhase1 = false;
  }

  if (isCkptPending()) {
    stop(comm);
  }
  setCurrState(IN_CS);
}

void
TwoPhaseAlgo::commit_finish() {
  // INVARIANT: All of our peers have executed the real collective comm.
  // Now, we can re-enable checkpointing
  // setCurrState(PHASE_2);
  // if (isCkptPending()) {
  //   stop(comm, PHASE_2);
  // }
  setCurrState(IS_READY);
  wrapperExit();
}

void
TwoPhaseAlgo::preSuspendBarrier(const void *data)
{
  JASSERT(data).Text("Pre-suspend barrier called with NULL data!");
  DmtcpMessage msg(DMT_PRE_SUSPEND_RESPONSE);

  phase_t st = getCurrState();
  query_t query = *(query_t*)data;
  static int procRank = -1;

  if (procRank == -1) {
    JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &procRank) == MPI_SUCCESS &&
            procRank != -1);
  }

  switch (query) {
    case INTENT:
      setCkptPending();
      // FIXME: review and add comment
      if (entering_phase1) {
        while (waitForSafeState() != PHASE_1 && waitForSafeState() != IN_CS);
      }
#if 0
    case GET_STATUS:
      if (isInWrapper()) {
        st = waitForSafeState();
      }
#endif
      break;
    case FREE_PASS:
      phase1_freepass = true;
      // FIXME: if we have the freepass and we are in the trivial barrier or
      // phase 1, we can report that we are in the critical section, because
      // we will be soon.
      while (waitForSafeState() == PHASE_1);
      break;
#if 0
    case CKPT:
      setRecvdCkptMsg();
      phase1_freepass = true;
      if (isInWrapper()) {
        if (isInBarrier()) {
          setCurrState(READY_FOR_CKPT);
        } else {
          st = waitForFreePassToTakeEffect(st);
        }
      } else {
        setCurrState(READY_FOR_CKPT);
      }
      break;
#endif
    case WAIT_STRAGGLER:
      waitForSafeState();
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinatory");
      break;
  }
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  st = getCurrState();
  // Maybe the user thread enters the wrapper only here.
  // So, we report an obsolete state (IS_READY).  But it is still
  // a consistent snapshot.  If IS_READY, we ignore the _comm.
  // FIXME: This assumes sequential memory consistency.  Maybe add fence()?
  int gid = VirtualGlobalCommId::instance().getGlobalId(_comm);
  _commAndStateMutex.unlock();
  rank_state_t state = { .rank = procRank, .comm = gid, .st = st};
  JASSERT(state.comm != MPI_COMM_NULL || state.st == IS_READY)
	 (state.comm)(state.st)(gid)(_currState)(query);
  JTRACE("Sending DMT_PRE_SUSPEND_RESPONSE message")(procRank)(gid)(st);
  msg.extraBytes = sizeof state;
  informCoordinatorOfCurrState(msg, &state);
}


// Local functions

bool
TwoPhaseAlgo::isInBarrier() {
  lock_t lock(_phaseMutex);
  return _currState == IN_TRIVIAL_BARRIER;
}

void
TwoPhaseAlgo::stop(MPI_Comm comm)
{
  // We got here because we must have received a ckpt-intent msg from
  // the coordinator
  // INVARIANT: Ckpt should be pending when we get here
  JASSERT(isCkptPending());
  setCurrState(PHASE_1);
  entering_phase1 = false;

  while (isCkptPending() && !phase1_freepass) {
    sleep(1);
  }
  phase1_freepass = false;
#if 0

  Finally, we wait for a ckpt msg from the coordinator if we are
  in a checkpoint-able state.
  if (isCkptPending() && getCurrState() != IN_CS && recvdCkptMsg()) {
    waitForCkpt();
  }
#endif
}

// FIXME: not used, remove when code is stable
bool
TwoPhaseAlgo::waitForFreePass(MPI_Comm comm)
{
  // This is called by stop() in phase-2
  lock_t lock(_freePassMutex);
  _freePassCv.wait(lock, [this]{ return _freePass; });
  bool tmp = _freePass;
  _freePass = false; // Clear out free pass for next checkpoint
  return tmp;
}

// FIXME: not used, remove when code is stable
void
TwoPhaseAlgo::waitForCkpt()
{
  setCurrState(READY_FOR_CKPT);
  while (isCkptPending()) {
    sleep(1);
  }
}

phase_t
TwoPhaseAlgo::waitForSafeState()
{
  lock_t lock(_phaseMutex);
  // The user thread will notify us if transition to any of these states
  _phaseCv.wait(lock, [this]{ return _currState == IN_TRIVIAL_BARRIER ||
                                     _currState == PHASE_1 ||
                                     _currState == IN_CS ||
                                     _currState == IS_READY; });
  return _currState;
}

// FIXME: not used, remove when code is stable
phase_t
TwoPhaseAlgo::waitForFreePassToTakeEffect(phase_t oldState)
{
  lock_t lock(_phaseMutex);
  _phaseCv.wait(lock, [this, oldState]{ return _currState == READY_FOR_CKPT ||
                                               _currState == IN_CS ||
                                               (oldState == PHASE_2 &&
                                                _currState == PHASE_1) ||
                                               (oldState == PHASE_1 &&
                                                _currState == PHASE_2) ||
                                               _currState == IS_READY ||
                                               _currState == IN_TRIVIAL_BARRIER;});
  return _currState;
}

bool
TwoPhaseAlgo::informCoordinatorOfCurrState(const DmtcpMessage& msg,
                                           const void *extraData)
{
  // This is a weird API.. doesn't return success or failure
  CoordinatorAPI::sendMsgToCoordinator(msg, extraData, msg.extraBytes);
  return true;
}

void
TwoPhaseAlgo::wrapperEntry(MPI_Comm comm)
{
  lock_t lock(_wrapperMutex);
  _inWrapper = true;
  _comm = comm;
}

void
TwoPhaseAlgo::wrapperExit()
{
  lock_t lock(_wrapperMutex);
  JASSERT(_currState == IS_READY);
  _inWrapper = false;
  _comm = MPI_COMM_NULL;
}
