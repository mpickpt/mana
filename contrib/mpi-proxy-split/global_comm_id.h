#ifndef GLOBAL_COMM_ID_H
#define GLOBAL_COMM_ID_H
#include <unordered_map>
#include "virtual-ids.h"

void localGetCommWorldRanks(MPI_Comm comm, int *rankArray);
int localRankToGlobalRank(int localRank, MPI_Comm localComm);

class VirtualGlobalCommId {
  public:
    unsigned int createGlobalId(MPI_Comm comm);

    unsigned int getGlobalId(MPI_Comm comm) {
      std::unordered_map<MPI_Comm, unsigned int>::iterator it =
        globalIdTable.find(comm);
      JASSERT(it != globalIdTable.end())(comm)
        .Text("Can't find communicator in the global id table");
      return it->second;
    }

    void removeGlobalId(MPI_Comm comm) {
      globalIdTable.erase(comm);
    }

    static VirtualGlobalCommId& instance() {
      static VirtualGlobalCommId _vGlobalId;
      return _vGlobalId;
    }

  private:
    VirtualGlobalCommId()
    {
      globalIdTable[MPI_COMM_NULL] = MPI_COMM_NULL;
      globalIdTable[MPI_COMM_WORLD] = MPI_COMM_WORLD;
    }

    void printMap(bool flag = false) {
      for (std::pair<MPI_Comm, int> idPair : globalIdTable) {
        if (flag) {
          printf("virtual comm: %x, real comm: %x, global id: %x\n",
              idPair.first, VIRTUAL_TO_REAL_COMM(idPair.first),
              idPair.second);
          fflush(stdout);
        } else {
          JTRACE("Print global id mapping")((void*) (uint64_t) idPair.first)
            ((void*) (uint64_t) VIRTUAL_TO_REAL_COMM(idPair.first))
            ((void*) (uint64_t) idPair.second);
        }
      }
    }
    // from https://stackoverflow.com/questions/664014/
    // what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
    int hash(int i) {
      return i * 2654435761 % ((unsigned long)1 << 32);
    }
    std::unordered_map<MPI_Comm, unsigned int> globalIdTable;
};

#endif
