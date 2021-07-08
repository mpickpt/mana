#include <ucontext.h>
#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "siginfo.h"
#if 0
#include "split-process.h"

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

struct twoPhaseHistory {
  int lineNo;
  int _comm;
  int comm;
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

#if 0
// Save and restore global variables that may be changed during
// restart events. These are called from mpi_plugin.cpp using
// DMTCP_PRIVATE_BARRIER_RESTART.
int inTrivialBarrierOrPhase1_copy = -1;
ucontext_t beforeTrivialBarrier_copy;
#endif

void
save2pcGlobals()
{
#if 0
  inTrivialBarrierOrPhase1_copy = inTrivialBarrierOrPhase1;
  beforeTrivialBarrier_copy = beforeTrivialBarrier;
#endif
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
}

void
restore2pcGlobals()
{
#if 0
  inTrivialBarrierOrPhase1 = inTrivialBarrierOrPhase1_copy;
  beforeTrivialBarrier = beforeTrivialBarrier_copy;
#endif
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
}

using namespace dmtcp_mpi;

int
TwoPhaseAlgo::commit(MPI_Comm comm, const char *collectiveFnc,
                     std::function<int(void)>doRealCollectiveComm)
{
  if (!LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  JTRACE("Invoking 2PC for")(collectiveFnc);
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
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  wrapperEntry(comm);
  setCurrState(IN_TRIVIAL_BARRIER);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  _commAndStateMutex.unlock();

  // Call the trivial barrier
  DMTCP_PLUGIN_DISABLE_CKPT();
  // Set state again incase we returned from beforeTrivialBarrier
  setCurrState(IN_TRIVIAL_BARRIER);
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
#if 0
  while (!flag && request != MPI_REQUEST_NULL) {
    int rc;
    rc = MPI_Test(&request, &flag, MPI_STATUS_IGNORE);
#ifdef DEBUG
    JASSERT(rc == MPI_SUCCESS)(rc)(realComm)(request)(comm);
#endif
    // FIXME: make this smaller
    struct timespec test_interval = {.tv_sec = 0, .tv_nsec = 100000};
    nanosleep(&test_interval, NULL);
  }
#else
  MPI_Wait(&_request, MPI_STATUS_IGNORE);
#endif

  if (isCkptPending()) {
    stop(comm);
  }
  REMOVE_OLD_REQUEST(_request);
  _request = MPI_REQUEST_NULL;
  setCurrState(IN_CS);
}

void
TwoPhaseAlgo::commit_finish()
{
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  setCurrState(IS_READY);
  wrapperExit();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  _commAndStateMutex.unlock();
}

void
TwoPhaseAlgo::logIbarrierIfInTrivBarrier()
{
  phase_t st = getCurrState();
  if (st == IN_TRIVIAL_BARRIER || (st == PHASE_1 && !phase1_freepass)) {
    LOG_CALL(restoreRequests, Ibarrier, _comm, _request);
  }
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
      if (getCurrState() == PHASE_1) {
        while (waitForNewState() != IN_CS);
      }
      break;
    case FREE_PASS:
      phase1_freepass = true;
      while (waitForNewState() == PHASE_1);
      break;
    case WAIT_STRAGGLER:
      waitForNewState();
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinatory");
      break;
  }
  _commAndStateMutex.lock();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  // maintain consistent view for DMTCP coordinator
  st = getCurrState();
  int gid = VirtualGlobalCommId::instance().getGlobalId(_comm);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  _commAndStateMutex.unlock();
  rank_state_t state = { .rank = procRank, .comm = gid, .st = st};
  _commAndStateMutex.lock();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  JASSERT(state.comm != MPI_COMM_NULL || state.st == IS_READY)
	 (state.comm)(state.st)(gid)(_currState)(query);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  _commAndStateMutex.unlock();
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

  while (isCkptPending() && !phase1_freepass) {
    sleep(1);
  }
  phase1_freepass = false;
}

phase_t
TwoPhaseAlgo::waitForNewState()
{
  lock_t lock(_phaseMutex);
  // The user thread will notify us if transition to any of these states
  _phaseCv.wait(lock, [this]{ return _currState == IN_TRIVIAL_BARRIER ||
                                     _currState == PHASE_1 ||
                                     _currState == IN_CS ||
                                     _currState == IS_READY; });
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
