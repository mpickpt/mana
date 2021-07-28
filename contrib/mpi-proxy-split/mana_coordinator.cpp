#include <mpi.h>

#include <functional>
#include <numeric>
#include <algorithm>
#include <map>
#include <sstream>

#include "dmtcp_coordinator.h"
#include "lookup_service.h"
#include "mana_coordinator.h"
#include "mana_coord_proto.h"
#include "jtimer.h"

using namespace dmtcp;

// FIXME: For debugging, remove it after debugging
extern DmtcpCoordinator prog;

typedef dmtcp::map<dmtcp::CoordClient*, rank_state_t> ClientToStateMap;
typedef dmtcp::map<dmtcp::CoordClient*, phase_t> ClientToPhaseMap;
typedef ClientToStateMap::value_type RankKVPair;
typedef ClientToPhaseMap::value_type PhaseKVPair;
typedef ClientToStateMap::const_iterator RankMapConstIterator;
typedef ClientToPhaseMap::const_iterator PhaseMapConstIterator;

// FIXME: Why can't we use client states
typedef dmtcp::map<dmtcp::CoordClient*, int> ClientToRankMap;
typedef dmtcp::map<dmtcp::CoordClient*, unsigned int> ClientToGidMap;
static ClientToRankMap clientRanks;
static ClientToGidMap clientGids;

static ClientToStateMap clientStates;
static ClientToPhaseMap clientPhases;

static std::ostream& operator<<(std::ostream &os, const phase_t &st);
static bool noRankInState(const ClientToStateMap& clientStates, long int size,
                          phase_t state);
static void waitAllSeenIntent(const ClientToStateMap& clientStates,
                              long int size);
static void unblockRanks(const ClientToStateMap& clientStates, long int size);

JTIMER(twoPc);

void
printNonReadyRanks()
{
  ostringstream o;
  o << "Non-ready Rank States:" << std::endl;
  for (PhaseKVPair c : clientPhases) {
    phase_t st = c.second;
    CoordClient *client = c.first;
    if (st != IS_READY) {
      o << client->identity() << ": " << st << std::endl;
    }
  }
  printf("%s\n", o.str().c_str());
  fflush(stdout);
}

#if 1
string
getClientState(CoordClient *client)
{
  ostringstream o;
# if 0
  o << ", " << clientStates[client].rank
    << "/" << clientStates[client].st
    << "/" << (void *)clientStates[client].comm;
# else
  o << ", " << clientRanks[client]
    << "/" << clientPhases[client]
    << "/" << (void *)(unsigned long)clientGids[client];
# endif
  return o.str();
}
#else
string
getClientState(CoordClient *client)
{
  ostringstream o;
  o << ", " << clientPhases[client];
  return o.str();
}
#endif

void
processPreSuspendClientMsgHelper(DmtcpCoordinator *coord,
                                 CoordClient *client,
                                 int &workersAtCurrentBarrier,
                                 const DmtcpMessage& msg,
                                 const void *extraData)
{
  static bool firstTime = true;
  if (firstTime) {
    JTIMER_START(twoPc);
    firstTime = false;
  }
  ComputationStatus status = coord->getStatus();

  // Verify correctness of the response
  if (msg.extraBytes != sizeof(rank_state_t) || !extraData) {
    JWARNING(false)(msg.from)(msg.state)(msg.extraBytes)
            .Text("Received msg with no (or invalid) state information!");
    return;
  }

  // First, insert client's response in our map
  rank_state_t state = *(rank_state_t*)extraData;
  clientStates[client] = state;
  clientPhases[client] = state.st;
  // FIXME: Why can't we use `state'
  clientRanks[client] = state.rank;
  clientGids[client] = state.comm;

  JTRACE("Received pre-suspend response from client")(msg.from)(state.st)
        (state.comm)(state.rank);

  // Next, return early, if we haven't received acks from all clients
  if (!status.minimumStateUnanimous ||
      workersAtCurrentBarrier < status.numPeers) {
    return;
  }
  JTRACE("Received pre-suspend response from all ranks");
  printf("%s\n", prog.printList().c_str());

  JTRACE("Checking if we can send the checkpoint message");
  if (noRankInState(clientStates, status.numPeers, IN_CS_INTENT_WASNT_SEEN)
      && noRankInState(clientStates, status.numPeers, IN_CS_NO_TRIV_BARRIER)
      && noRankInState(clientStates, status.numPeers, IN_CS)
      && noRankInState(clientStates, status.numPeers, HYBRID_PHASE1)) {
    JTRACE("All ranks ready for checkpoint; broadcasting suspend msg...");
    JTIMER_STOP(twoPc);
    coord->startCheckpoint();
    goto done;
  }

#ifdef HYBRID_2PC
  JTRACE("Checking if any rank haven't seen INTENT");
  if (!noRankInState(clientStates, status.numPeers, IN_CS_INTENT_WASNT_SEEN)) {
    printf("Some rank in IN_CS_INTENT_WASNT_SEEN");
    waitAllSeenIntent(clientStates, status.numPeers);
    goto done;
  }
#endif

  JTRACE("Trying to unblocks ranks in PHASE_1");
  unblockRanks(clientStates, status.numPeers);
done:
  workersAtCurrentBarrier = 0;
  clientStates.clear();
}

void
sendCkptIntentMsg(dmtcp::DmtcpCoordinator *coord)
{
  query_t q(INTENT);
  coord->broadcastMessage(DMT_DO_PRE_SUSPEND, sizeof q, &q);
}

static std::ostream&
operator<<(std::ostream &os, const phase_t &st)
{
  switch (st) {
    case ST_ERROR           : os << "ST_ERROR"; break;
    case ST_UNKNOWN         : os << "ST_UNKNOWN"; break;
    case IS_READY           : os << "IS_READY"; break;
    case HYBRID_PHASE1      : os << "HYBRID_PHASE1"; break;
    case PHASE_1            : os << "PHASE_1"; break;
    case IN_CS              : os << "IN_CS"; break;
    case IN_CS_INTENT_WASNT_SEEN : os << "IN_CS_INTENT_WASNT_SEEN"; break;
    case IN_CS_NO_TRIV_BARRIER : os << "IN_CS_NO_TRIV_BARRIER"; break;
    case IN_TRIVIAL_BARRIER : os << "IN_TRIVIAL_BARRIER"; break;
    default                 : os << "Unknown state"; break;
  }
  return os;
}

static bool
noRankInState(const ClientToStateMap& clientStates, long int size,
              phase_t state)
{
  for (RankKVPair c : clientStates) {
    if (c.second.st == state) {
      return false;
    }
  }
  return true;
}

static void
waitAllSeenIntent(const ClientToStateMap& clientStates, long int size)
{
  query_t *queries = (query_t*) malloc(size * sizeof(query_t));
  memset(queries, NONE, size * sizeof(query_t));

  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] != NONE) {
      continue; // the message is already decided
    }
    if (c.second.st == HYBRID_PHASE1) {
      queries[c.second.rank] = FREE_PASS;
    }
  }

  // other ranks just wait for a iteration.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] == NONE) {
      queries[c.second.rank] = CONTINUE;
    }
  }

  // send all queires
  for (RankKVPair c : clientStates) {
    DmtcpMessage msg(DMT_DO_PRE_SUSPEND);
    query_t q = queries[c.second.rank];
    printf("Sending %d to rank %d\n", q, c.second.rank);
    fflush(stdout);
    JTRACE("Sending query to client")(q)
      (c.second.rank)(c.second.comm)(c.second.st);
    msg.extraBytes = sizeof q;
    c.first->sock() << msg;
    c.first->sock().writeAll((const char*)&q, msg.extraBytes);
  }
  free(queries);
}

static void
unblockRanks(const ClientToStateMap& clientStates, long int size)
{
  query_t *queries = (query_t*) malloc(size * sizeof(query_t));
  memset(queries, NONE, size * sizeof(query_t));

#ifdef HYBRID_2PC
  // If some member of a communicator is in IN_CS_NO_TRIV_BARRIER,
  // then all other group members with state HYBRID_PHASE1 gets a
  // DO_TRIV_BARRIER msg.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] != NONE) {
      continue; // the message is already decided
    }
    if (c.second.st == IN_CS_NO_TRIV_BARRIER) {
      queries[c.second.rank] = CONTINUE;
      for (RankKVPair other : clientStates) {
        if (other.second.comm == c.second.comm &&
            other.second.st == HYBRID_PHASE1) {
          queries[other.second.rank] = FREE_PASS;
        }
      }
    }
  }

  // If a group member is in HYBRID_PHASE1, and no group member is
  // in IN_CS_INTENT_WASNT_SEEN or int IN_CS_NO_TRIV_BARRIER, then
  // the group member in HYBRID_PHASE1 gets a DO_TRIV_BARRIER msg.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] != NONE) {
      continue; // the message is already decided
    }
    if (c.second.st == HYBRID_PHASE1) {
      bool noInCs = true;
      for (RankKVPair other : clientStates) {
        if (other.second.comm == c.second.comm
            && (other.second.st == IN_CS_INTENT_WASNT_SEEN
                || other.second.st == IN_CS_NO_TRIV_BARRIER)) {
          noInCs = false;
          break;
        }
      }
      if (noInCs) {
        queries[c.second.rank] = DO_TRIV_BARRIER;
      }
    }
  }
#endif

  // if some member of a communicator is in the critical section,
  // give free passes to members in phase 1 or the trivial barrier.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] != NONE) {
      continue; // the message is already decided
    }
    if (c.second.st == IN_CS) {
      queries[c.second.rank] = CONTINUE;
      for (RankKVPair other : clientStates) {
        if (other.second.comm == c.second.comm &&
            other.second.st == PHASE_1) {
          queries[other.second.rank] = FREE_PASS;
        }
      }
    }
  }

  // other ranks just wait for a iteration.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] == NONE) {
      queries[c.second.rank] = CONTINUE;
    }
  }

  // send all queires
  for (RankKVPair c : clientStates) {
    DmtcpMessage msg(DMT_DO_PRE_SUSPEND);
    query_t q = queries[c.second.rank];
    printf("Sending %d to rank %d\n", q, c.second.rank);
    fflush(stdout);
    JTRACE("Sending query to client")(q)
      (c.second.rank)(c.second.comm)(c.second.st);
    msg.extraBytes = sizeof q;
    c.first->sock() << msg;
    c.first->sock().writeAll((const char*)&q, msg.extraBytes);
  }
  free(queries);
}
