#include <stdio.h>
#include <mpi.h>
#include <unordered_map>

#include "split_process.h"
#include "dmtcp.h"
#include "global_comm_id.h"

// FIXME: The new name should be: GlobalIdOfSimiliarComm
void localGetCommWorldRanks(MPI_Comm comm, int *rankArray) {
  int size;
  MPI_Comm_size(comm, &size);
  bool debugging = false;
  if (rankArray == NULL) {
    debugging = true;
    rankArray = (int*)malloc(size * sizeof(int));
  }
  int localRanks[size];
  for (int i = 0; i < size; i++) {
    localRanks[i] = i;
  }
  MPI_Group worldGroup, localGroup;
  MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);
  MPI_Comm_group(comm, &localGroup);
  MPI_Group_translate_ranks(localGroup, size, localRanks,
      worldGroup, rankArray); 
  if (debugging) {
    for (int i = 0; i < size; i++) {
      printf("local rank: %d, global rank: %d\n", i, rankArray[i]);
    }
    fflush(stdout);
    free(rankArray);
  }
}

int
localRankToGlobalRank(int localRank, MPI_Comm localComm)
{
  int worldRank;
  MPI_Group worldGroup, localGroup;
  MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);
  MPI_Comm_group(localComm, &localGroup);
  MPI_Group_translate_ranks(localGroup, 1, &localRank,
                            worldGroup, &worldRank); 
  return worldRank;
}

unsigned int
VirtualGlobalCommId::createGlobalId(MPI_Comm comm)
{
  if (comm == MPI_COMM_NULL) {
    return comm;
  }
  unsigned int gid = 0;
  int worldRank, commSize;
  int realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
  MPI_Comm_size(comm, &commSize);
  int rbuf[commSize];
  // FIXME: cray cc complains "catastrophic error" that can't find
  // split-process.h
#if 1
  localGetCommWorldRanks(comm, rbuf);
#elif 1
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Allgather)(&worldRank, 1, MPI_INT,
      rbuf, 1, MPI_INT, realComm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
#else
  MPI_Allgather(&worldRank, 1, MPI_INT, rbuf, 1, MPI_INT, comm);
#endif
  for (int i = 0; i < commSize; i++) {
    gid ^= hash(rbuf[i]);
  }
  // FIXME: We assume the hash collision between communicators who
  // have different members is low.
  // FIXME: We want to prune virtual communicators to avoid long
  // restart time.
  // FIXME: In VASP we observed that for the same virtual communicator
  // (adding 1 to each new communicator with the same rank members),
  // the virtual group can change over time, using:
  // virtual Comm -> real Comm -> real Group -> virtual Group
  // We don't understand why since vasp does not seem to free groups.
#if 0
  // FIXME: Some code can create new communicators during execution,
  // and so hash conflict may occur later.
  // if the new gid already exists in the map, add one and test again
  while (1) {
    bool found = false;
    for (std::pair<MPI_Comm, unsigned int> idPair : globalIdTable) {
      if (idPair.second == gid) {
        found = true;
        break;
      }
    }
    if (found) {
      gid++;
    } else {
      break;
    }
  }
#endif
  globalIdTable[comm] = gid;
  return gid;
}
