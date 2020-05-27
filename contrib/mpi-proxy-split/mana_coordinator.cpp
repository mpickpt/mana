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

typedef dmtcp::map<dmtcp::CoordClient*, rank_state_t> ClientToStateMap;
typedef dmtcp::map<dmtcp::CoordClient*, phase_t> ClientToPhaseMap;
typedef ClientToStateMap::value_type RankKVPair;
typedef ClientToPhaseMap::value_type PhaseKVPair;
typedef ClientToStateMap::const_iterator RankMapConstIterator;
typedef ClientToPhaseMap::const_iterator PhaseMapConstIterator;

static ClientToStateMap clientStates;
static ClientToPhaseMap clientPhases;

static std::ostream& operator<<(std::ostream &os, const phase_t &st);
static bool noRanksInCriticalSection(const ClientToStateMap& clientStates);
static bool allRanksReadyForCkpt(const ClientToStateMap& clientStates, long int size);
static bool allRanksReady(const ClientToStateMap& clientStates, long int size);
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
    if (st != IS_READY && st != READY_FOR_CKPT) {
      o << client->identity() << ": " << st << std::endl;
    }
  }
  printf("%s\n", o.str().c_str());
  fflush(stdout);
}

void
printMpiDrainStatus(const LookupService& lookupService)
{
  const KeyValueMap* map = lookupService.getMap(MPI_SEND_RECV_DB);
  if (!map) {
    JTRACE("No send recv database");
    return;
  }

  typedef std::pair<KeyValue, KeyValue *> KVPair;
  std::function<uint64_t(uint64_t, KVPair)> sendSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->sends; };
  std::function<uint64_t(uint64_t, KVPair)> recvSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->recvs; };
  std::function<string(string, KVPair)> indivStats =
                    [](string str, KVPair el)
                    { send_recv_totals_t *obj =
                              (send_recv_totals_t*)el.second->data();
                      ostringstream o;
                      o << str
                        <<  "Rank-" << std::to_string(obj->rank) << ": "
                        << std::to_string(obj->sends) << ", "
                        << std::to_string(obj->recvs) << "; ";
                      return o.str(); };
  uint64_t totalSends = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, sendSum);
  uint64_t totalRecvs = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, recvSum);
  string individuals = std::accumulate(map->begin(), map->end(),
                                       string(""), indivStats);
  ostringstream o;
  o << MPI_SEND_RECV_DB << ": Total Sends: " << totalSends << "; ";
  o << "Total Recvs: " << totalRecvs << std::endl;
  o << "  Individual Stats: " << individuals;
  printf("%s\n", o.str().c_str());
  fflush(stdout);

  const KeyValueMap* map2 = lookupService.getMap(MPI_WRAPPER_DB);
  if (!map2) {
    JTRACE("No wrapper database");
    return;
  }

  std::function<string(string, KVPair)> rankStats =
                    [](string str, KVPair el)
                    { wr_counts_t *obj = (wr_counts_t*)el.second->data();
                      int rank = *(int*)el.first.data();
                      ostringstream o;
                      o << str
                        <<  "Rank-" << std::to_string(rank) << ": "
                        << std::to_string(obj->sendCount) << ", "
                        << std::to_string(obj->isendCount) << ", "
                        << std::to_string(obj->recvCount) << ", "
                        << std::to_string(obj->irecvCount) << ", "
                        << std::to_string(obj->sendrecvCount) << "; ";
                      return o.str(); };

  individuals = std::accumulate(map->begin(), map->end(),
                                string(""), rankStats);
  ostringstream o2;
  o2 << MPI_WRAPPER_DB << std::endl;
  o2 << "  Individual Stats: " << individuals;
  printf("%s\n", o2.str().c_str());
  fflush(stdout);
}

string
getClientPhase(CoordClient *client)
{
  ostringstream o;
  o << ", " << clientPhases[client];
  return o.str();
}

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

  JTRACE("Received pre-suspend response from client")(msg.from)(state.st);

  // Next, return early, if we haven't received acks from all clients
  if (!status.minimumStateUnanimous ||
      workersAtCurrentBarrier < status.numPeers) {
    return;
  }
  JTRACE("Received pre-suspend response from all ranks");

  // Initiate checkpointing, if all clients are ready and have
  // responded with their approvals.
  if (allRanksReadyForCkpt(clientStates, status.numPeers)) {
    JTRACE("All ranks ready for checkpoint; broadcasting suspend msg...");
    JTIMER_STOP(twoPc);
    coord->startCheckpoint();
    goto done;
  }

  // We are ready! If all clients responded with IN_READY, inform the
  // ranks of the imminent checkpoint msg and wait for their final approvals...
  if (allRanksReady(clientStates, status.numPeers)) {
    JTRACE("All ranks ready for checkpoint; broadcasting ckpt msg...");
    query_t q(CKPT);
    coord->broadcastMessage(DMT_DO_PRE_SUSPEND, sizeof q, &q);
    goto done;
  }

  // Nothing to do if no rank is in critical section
  if (noRanksInCriticalSection(clientStates)) {
    JTRACE("No ranks in critical section; nothing to do...");
    query_t q(GET_STATUS);
    coord->broadcastMessage(DMT_DO_PRE_SUSPEND, sizeof q, &q);
    goto done;
  }

  // Finally, if there are ranks stuck in PHASE_1 or in PHASE_2 (is PHASE_2
  // required??), unblock them. This can happen only if there are ranks
  // executing in critical section.
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
    case UNKNOWN        : os << "UNKNOWN"; break;
    case IS_READY       : os << "IS_READY"; break;
    case PHASE_1        : os << "PHASE_1"; break;
    case IN_CS          : os << "IN_CS"; break;
    case OUT_CS         : os << "OUT_CS"; break;
    case READY_FOR_CKPT : os << "READY_FOR_CKPT"; break;
    case PHASE_2        : os << "PHASE_2"; break;
    default             : os << "Unknown state"; break;
  }
  return os;
}

static bool
noRanksInCriticalSection(const ClientToStateMap& clientStates)
{
  RankMapConstIterator req =
    std::find_if(clientStates.begin(), clientStates.end(),
                 [](const RankKVPair& elt)
                 { return elt.second.st == IN_CS; });
  return req == std::end(clientStates);
}

static bool
allRanksReadyForCkpt(const ClientToStateMap& clientStates, long int size)
{
  int numReadyRanks =
        std::count_if(clientStates.begin(), clientStates.end(),
                      [](const RankKVPair& elt)
                      { return elt.second.st == READY_FOR_CKPT; });
  JTRACE("Ranks ready for ckpting")(numReadyRanks)(size);
  return numReadyRanks == size;
}

static bool
allRanksReady(const ClientToStateMap& clientStates, long int size)
{
  int numReadyRanks =
        std::count_if(clientStates.begin(), clientStates.end(),
                      [](const RankKVPair& elt)
                      { return elt.second.st == IS_READY; });
  int numPhase1Ranks =
        std::count_if(clientStates.begin(), clientStates.end(),
                      [](const RankKVPair& elt)
                      { return elt.second.st == PHASE_1; });
  int numPhase2Ranks =
        std::count_if(clientStates.begin(), clientStates.end(),
                      [](const RankKVPair& elt)
                      { return elt.second.st == PHASE_2; });
  return ((numReadyRanks + numPhase1Ranks) == size) ||
         ((numReadyRanks + numPhase1Ranks + numPhase2Ranks) == size) ||
         (numPhase2Ranks == size);
}

static void
unblockRanks(const ClientToStateMap& clientStates, long int size)
{
  int count = 0;
  for (RankKVPair c : clientStates) {
    DmtcpMessage msg(DMT_DO_PRE_SUSPEND);
    if (c.second.st == PHASE_1 || c.second.st == PHASE_2) {
      // For ranks in PHASE_1 or in PHASE_2, send them a free pass to unblock
      // them.
      JTRACE("Sending free pass to client")(c.first->identity())(c.second.st);
      query_t q(FREE_PASS);
      msg.extraBytes = sizeof q;
      c.first->sock() << msg;
      c.first->sock().writeAll((const char*)&q, msg.extraBytes);
      count++;
    } else if (c.second.st == IN_CS || c.second.st == IS_READY) {
      // For other ranks in critical section or in ready state, just send
      // them an info msg.
      query_t q(GET_STATUS);
      msg.extraBytes = sizeof q;
      c.first->sock() << msg;
      c.first->sock().writeAll((const char*)&q, msg.extraBytes);
      count++;
    } else {
      JWARNING(false)(c.first->identity())(c.second.st)
              .Text("Rank in unhandled state");
    }
  }
  JTRACE("Unblocked ranks")(count);
}
