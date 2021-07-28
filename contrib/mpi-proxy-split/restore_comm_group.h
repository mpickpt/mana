#ifndef RESTORE_COMM_GROUP_H
#define RESTORE_COMM_GROUP_H
#include <mpi.h>
#include <unordered_set>
#include <unordered_map>

typedef struct __group_members_t
{
  MPI_Group group_id;
  int *world_ranks;
  int size;
} group_members_t;

extern void initialize_comm_group();
extern void recordCommGroups();
extern void restoreCommGroups();
extern group_members_t* getGroupMembers(MPI_Group group);

extern std::unordered_set<MPI_Comm> active_comms;
extern std::unordered_map<MPI_Group, group_members_t*> active_groups;
extern MPI_Group worldGroup;

#endif
