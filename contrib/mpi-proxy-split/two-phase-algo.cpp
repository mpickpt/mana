#include <ucontext.h>
#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "siginfo.h"
#include "global_comm_id.h"
#include "mpi_nextfunc.h"
#include "p2p_log_replay.h"

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

  MPI_Comm_size(comm, &size);
  rankArray = (int*)malloc(size * sizeof(int));
  localGetCommWorldRanks(comm, rankArray);

#ifdef HYBRID_2PC
  if (isCkptPending()) {
    if (!_do_triv_barrier) {
      stop(HYBRID_PHASE1);
    }
    // check _do_triv_barrier again to tell if we
    // need to do the trivial barrier or not.
    if (_do_triv_barrier) {
#endif
      trivialBarrier(comm);
      if (isCkptPending()) {
        stop(PHASE_1);
      } else {
        setCurrState(IN_CS);
      }
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
  if (isCkptPending()) {
    stop(FINISHED_PHASE2);
  } else {
    _commAndStateMutex.lock();
    setCurrState(IS_READY);
    _comm = MPI_COMM_NULL;
    _commAndStateMutex.unlock();
  }
  free(rankArray);
}


void
TwoPhaseAlgo::trivialBarrier(MPI_Comm comm) {
  // Call the trivial barrier
  DMTCP_PLUGIN_DISABLE_CKPT();
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
  while (!flag && _request != MPI_REQUEST_NULL && isCkptPending()) {
    MPI_Test(&_request, &flag, MPI_STATUS_IGNORE);
  }
  if (_request != MPI_REQUEST_NULL) {
    REMOVE_OLD_REQUEST(_request);
    _request = MPI_REQUEST_NULL;
  }
#else
  MPI_Wait(&_request, MPI_STATUS_IGNORE);
#endif
}

void
TwoPhaseAlgo::logIbarrierIfInTrivBarrier()
{
  JASSERT(!_freepass)
    .Text("_freepass should be false when about to checkpoint");
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
        // There's a gap after the stop() function and before
        // changing state to IN_CS. If we report this rank is
        // still in PHASE_1, it will get an extra freepass.
        // So we wait for 1 second. If the user thread is in the
        // gap, it's enough for the user thread to move to the
        // next state, IN_CS.
        sleep(1);
      }
#endif
      break;
    case DO_TRIV_BARRIER:
      // All ranks received DO_TRIV_BARRIER. This will not block.
      _do_triv_barrier = true;
      // If received DO_TRIV_BARRIER, we should also unblock ranks
      // in HYBRID_PHASE1. We don't have break statement here.
      // So we should also execute the code for the FREE_PASS case.
      if (st != HYBRID_PHASE1) {
        break;
      }
    case FREE_PASS:
      _freepass = true;
      // The checkpoint thread must report a new state to the coordinator.
      // The new state may be same as the current state, and the comm can
      // be the same or different. But we know that _freepass will
      // have been changed to false. To guarentee this, the user thread
      // must change its state and update to the latest comm before setting
      // _freepass to false.
      while (_freepass) { sleep(1); }
      break;
    case CONTINUE:
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
  CoordinatorAPI::sendMsgToCoordinator(msg, &state, msg.extraBytes);
}

void
TwoPhaseAlgo::stop(phase_t state)
{
  // We got here because we must have received a ckpt-intent msg from
  // the coordinator
  // INVARIANT: Ckpt should be pending when we get here
  JASSERT(isCkptPending());
  // stop() can only be called in these HYBRID_PHASE1, PHASE_1,
  // or FINISHED_PHASE2.
  JASSERT(state == HYBRID_PHASE1 ||
          state == PHASE_1 ||
          state == FINISHED_PHASE2);
  setCurrState(state);

  while (isCkptPending() && !_freepass) {
    sleep(1);
  }

  // We must change the state before unsetting the _freepass,
  // so that the ckpt thread can report the newer state to the coordinator.
  if (state == PHASE_1) {
    setCurrState(IN_CS);
  } else if (state == HYBRID_PHASE1) {
    if (_do_triv_barrier) {
      setCurrState(IN_TRIVIAL_BARRIER);
    } else {
      setCurrState(IN_CS_NO_TRIV_BARRIER);
    }
  } else { // state == FINISHED_PHASE2
    _commAndStateMutex.lock();
    setCurrState(IS_READY);
    _comm = MPI_COMM_NULL;
    _commAndStateMutex.unlock();
  }
  _freepass = false;
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
