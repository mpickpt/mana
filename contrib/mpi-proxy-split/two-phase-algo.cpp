#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "mpi_nextfunc.h"

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
  wrapperEntry(comm);
  JTRACE("Invoking 2PC for")(collectiveFnc);

  // Call the trivial barrier if it's the second time this
  // communicator enters a wrapper after get a free pass
  // from PHASE_2
  if (inCommHistory(comm)) {
    setCurrState(READY_FOR_CKPT);
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Barrier)(realComm);
    RETURN_TO_UPPER_HALF();
  }

  int retval;

  if (isCkptPending()) {
    stop(comm, PHASE_1);
  }
  setCurrState(IN_CS);
  retval = doRealCollectiveComm();
  // INVARIANT: All of our peers have executed the real collective comm.
  // Now, we can re-enable checkpointing
  setCurrState(OUT_CS);
  if (isCkptPending()) {
    stop(comm, PHASE_2);
  }
  setCurrState(IS_READY);
  wrapperExit();
  return retval;
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
    case GET_STATUS:
      if (isInWrapper()) {
        st = waitForSafeState();
      }
      break;
    case FREE_PASS:
      JASSERT(isInWrapper() && (st == PHASE_1 || st == PHASE_2))
             (procRank)(isInWrapper())(st);
      notifyFreePass();
      st = waitForFreePassToTakeEffect(st);
      break;
    case CKPT:
      setRecvdCkptMsg();
      notifyFreePass(true);
      if (isInWrapper()) {
        st = waitForFreePassToTakeEffect(st);
      } else {
        setCurrState(READY_FOR_CKPT);
      }
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinatory");
      break;
  }
  rank_state_t state = { .rank = procRank, .comm = _comm, .st = st};
  msg.extraBytes = sizeof state;
  informCoordinatorOfCurrState(msg, &state);
}


// Local functions

void
TwoPhaseAlgo::stop(MPI_Comm comm, phase_t p)
{
  // We got here because we must have received a ckpt-intent msg from
  // the coordinator
  // INVARIANT: We can checkpoint only within stop()
  JASSERT(p == PHASE_1 || p == PHASE_2);
  // INVARIANT: Ckpt should be pending when we get here
  JASSERT(isCkptPending());
  setCurrState(p);

  // Now, we must wait for a free pass from the coordinator; a free pass
  // indicates that all our peers are either ready or stuck in collective call
  waitForFreePass(comm);

  // If we are in PHASE_2, we will call the trivial barrier the next time we
  // enter a wrapper with the same communicator
  if (p == PHASE_2) {
    addCommHistory(comm);
  }

  // Finally, we wait for a ckpt msg from the coordinator if we are
  // in a checkpoint-able state.
  if (isCkptPending() && getCurrState() != IN_CS && recvdCkptMsg()) {
    waitForCkpt();
  }
}

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
  _phaseCv.wait(lock, [this]{ return _currState == PHASE_1 ||
                                     _currState == PHASE_2 ||
                                     _currState == READY_FOR_CKPT ||
                                     _currState == IN_CS;});
  return _currState;
}

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
                                               _currState == IS_READY;});
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
  _inWrapper = false;
  _comm = MPI_COMM_NULL;
}
