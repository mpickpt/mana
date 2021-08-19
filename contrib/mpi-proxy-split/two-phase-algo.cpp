#include <ucontext.h>
#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "siginfo.h"
#include "global_comm_id.h"
#include "mpi_nextfunc.h"

#define HYBRID_2PC
using namespace dmtcp_mpi;

struct twoPhaseHistory {
  int lineNo;
  int _comm;
  int comm;
  bool free_pass;
  phase_t state;
  phase_t currState;
};
#define commStateHistoryLength 100
struct twoPhaseHistory commStateHistory[commStateHistoryLength];
int commStateHistoryLast = -1;
// USAGE:
//  commStateHistoryAdd(
//    {.lineNo = __LINE__, .comm = comm, ._comm = _comm,
//     .state = state, .currState = getCurrState()});
//  Use _commAndStateMutex.lock/unlock if not already present.
void commStateHistoryAdd(struct twoPhaseHistory item) {
  commStateHistory[++commStateHistoryLast % commStateHistoryLength] = item;
}

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

void
logIbarrierIfInTrivBarrier()
{
  TwoPhaseAlgo::instance().logIbarrierIfInTrivBarrier();
}


void
save2pcGlobals()
{
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
}

void
restore2pcGlobals()
{
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
  TwoPhaseAlgo::instance().replayTrivialBarrier();
}

int
TwoPhaseAlgo::commit(MPI_Comm comm, const char *collectiveFnc,
                     std::function<int(void)>doRealCollectiveComm)
{
  if (!LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  if (!LOGGING()) {
    return doRealCollectiveComm();
  }

  commit_begin(comm);
  int retval = doRealCollectiveComm();
  commit_finish();
  return retval;
}

void
TwoPhaseAlgo::commit_begin(MPI_Comm comm)
{
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  _comm = comm;
  _commAndStateMutex.unlock();

#ifdef HYBRID_2PC
  if (isCkptPending()) {
    setCurrState(HYBRID_PHASE1);
    if (!do_triv_barrier) {
      stop(comm);
    }
    // checck do_triv_barrier again to tell if we
    // need to do the trivial barrier or not.
    if (!do_triv_barrier) {
      // We must change the state before unsetting the phase1_freepass,
      // so that the ckpt thread can report the newer state to the coordinator.
      setCurrState(IN_CS_NO_TRIV_BARRIER);
      phase1_freepass = false;
    } else {
#endif
      // Call the trivial barrier
      DMTCP_PLUGIN_DISABLE_CKPT();
      // We must change the state before unsetting the phase1_freepass,
      // so that the ckpt thread can report the newer state to the coordinator.
      setCurrState(IN_TRIVIAL_BARRIER);
      phase1_freepass = false;
      MPI_Request request;
      MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
      int flag = 0;
      int tb_rc = -1;
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      tb_rc = NEXT_FUNC(Ibarrier)(realComm, &request);
      RETURN_TO_UPPER_HALF();
      MPI_Request virtRequest = ADD_NEW_REQUEST(request);
      _request = virtRequest;
      JASSERT(tb_rc == MPI_SUCCESS)
        .Text("The trivial barrier in two-phase-commit algorithm failed");
      DMTCP_PLUGIN_ENABLE_CKPT();
#ifdef HYBRID_2PC
      // FIXME: If we can cancel a request, then we can avoid memory leaks
      // due to a stale MPI_Request when we abort a trivial barrier.
      while (!flag && _request != MPI_REQUEST_NULL && isCkptPending()) {
        int rc;
        rc = MPI_Test(&_request, &flag, MPI_STATUS_IGNORE);
#ifdef DEBUG
        JASSERT(rc == MPI_SUCCESS)(rc)(realComm)(_request)(comm);
#endif
        // FIXME: make this smaller
        struct timespec test_interval = {.tv_sec = 0, .tv_nsec = 100000};
        nanosleep(&test_interval, NULL);
      }
#else
      MPI_Wait(&_request, MPI_STATUS_IGNORE);
#endif
      if (isCkptPending()) {
        setCurrState(PHASE_1);
        stop(comm);
      }
      if (request != MPI_REQUEST_NULL) {
        REMOVE_OLD_REQUEST(_request);
        _request = MPI_REQUEST_NULL;
      }
      // We must change the state before unsetting the phase1_freepass,
      // so that the ckpt thread can report the newer state to the coordinator.
      setCurrState(IN_CS);
      phase1_freepass = false;
#ifdef HYBRID_2PC
    }
  } else {
    setCurrState(IN_CS_INTENT_WASNT_SEEN);
  }
#endif
}

void
TwoPhaseAlgo::commit_finish()
{
  _commAndStateMutex.lock();
  setCurrState(IS_READY);
  _comm = MPI_COMM_NULL;
  _commAndStateMutex.unlock();
}

void
TwoPhaseAlgo::logIbarrierIfInTrivBarrier()
{
  JASSERT(!phase1_freepass)
    .Text("phase1_freepass should be false when about to checkpoint");
  phase_t st = getCurrState();
  if (st == IN_TRIVIAL_BARRIER || st == PHASE_1) {
    _replayTrivialBarrier = true;
  } else {
    _replayTrivialBarrier = false;
  }
}

void
TwoPhaseAlgo::replayTrivialBarrier()
{
  if (_replayTrivialBarrier) {
    MPI_Request request;
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(_comm);
    int flag = 0;
    int tb_rc = -1;
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    tb_rc = NEXT_FUNC(Ibarrier)(realComm, &request);
    RETURN_TO_UPPER_HALF();
    UPDATE_REQUEST_MAP(_request, request);
    _replayTrivialBarrier = false;
  }
}

void
TwoPhaseAlgo::preSuspendBarrier(const void *data)
{
  JASSERT(data).Text("Pre-suspend barrier called with NULL data!");
  DmtcpMessage msg(DMT_PRE_SUSPEND_RESPONSE);

  phase_t st = getCurrState();
  query_t query = *(query_t*)data;

  switch (query) {
    case INTENT:
      setCkptPending();
#ifndef HYBRID_2PC
      if (getCurrState() == PHASE_1) {
        // If in PHASE_1, wait for us to finish doing IN_CS.
        // There's a gap after the stop() function and before
        // changing state to IN_CS. If we report this rank is
        // still in PHASE_1, it will get an extra freepass.
        // FIXME: wrong implementation. Change it after fixing the style
        phase_t newState = ST_UNKNOWN;
        newState = waitForNewStateAfter(ST_UNKNOWN);
        newState = IN_CS;
      }
#endif
      break;
    case DO_TRIV_BARRIER:
      // All ranks received DO_TRIV_BARRIER. This will not block.
      do_triv_barrier = true;
      // If received DO_TRIV_BARRIER, we should also unblock ranks
      // in HYBRID_PHASE1. We don't have break statement here.
      // So we should also execute the code for the FREE_PASS case.
    case FREE_PASS:
      phase1_freepass = true;
      // The checkpoint thread must report a new state to the coordinator.
      // The new state may be same as the current state, and the comm can
      // be the same or different. But we know that phase1_freepass will
      // have been changed to false. To guarentee this, the user thread
      // must change its state and update to the latest comm before setting
      // phase1_freepass to false.
      while (phase1_freepass) { sleep(1); }
      break;
    case CONTINUE:
      // Maybe some peers in critical section and some in PHASE_1 or
      // IN_TRIVIAL_BARRIER. This may happen if a checkpoint pending is
      // announced after some member already entered the critical
      // section. Then the coordinator will send
      // CONTINUE to those ranks in the critical section.
      // In this case, we just report back the current state.
      // But the user thread can continue running and enter IS_READY,
      // IN_TRIVIAL_BARRIER for a different communication, etc.
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinator");
      break;
  }
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  st = getCurrState();
  int globalId = VirtualGlobalCommId::instance().getGlobalId(_comm);
  _commAndStateMutex.unlock();

  rank_state_t state = { .rank = g_world_rank, .comm = globalId, .st = st};
  JASSERT(state.comm != MPI_COMM_NULL || state.st == IS_READY)
	 (state.comm)(state.st)(globalId)(_currState)(query);

  JTRACE("Sending DMT_PRE_SUSPEND_RESPONSE message")
        (g_world_rank)(globalId)(st);
  msg.extraBytes = sizeof state;
  informCoordinatorOfCurrState(msg, &state);
}


// Local functions

void
TwoPhaseAlgo::stop(MPI_Comm comm)
{
  // We got here because we must have received a ckpt-intent msg from
  // the coordinator
  // INVARIANT: Ckpt should be pending when we get here
  JASSERT(isCkptPending());
  while (isCkptPending() && !phase1_freepass) {
    sleep(1);
  }
}

phase_t
TwoPhaseAlgo::waitForNewStateAfter(phase_t oldState)
{
  lock_t lock(_phaseMutex);
  // The user thread will notify us if transition to any of these states
  _phaseCv.wait(lock, [this, oldState]
                      { return _currState != oldState; });
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
