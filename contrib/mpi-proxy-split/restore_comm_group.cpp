#include <mpi.h>
#include "restore_comm_group.h"
#include "split_process.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"

std::unordered_set<MPI_Comm> active_comms;
std::unordered_map<MPI_Group, group_members_t*> active_groups;
std::unordered_map<MPI_Comm, group_members_t*> comm_groups;
MPI_Group worldGroup;

void
initialize_comm_group()
{
  active_comms.insert(MPI_COMM_WORLD);
  active_comms.insert(MPI_COMM_SELF);
  MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);
}

group_members_t*
getGroupMembers(MPI_Group group)
{
  group_members_t *members = (group_members_t*) malloc(sizeof(group_members_t));
  members->group_id = group;
  MPI_Group_size(group, &members->size);
  members->world_ranks = (int*) malloc(members->size * sizeof(int));

  int localRanks[members->size];
  for (int i = 0; i < members->size; i++) {
    localRanks[i] = i;
  }
  MPI_Group_translate_ranks(group, members->size, localRanks,
                            worldGroup, members->world_ranks);
  return members;
}

void
recordCommGroups()
{
  std::unordered_set<MPI_Comm>::iterator comm;
  for (comm = active_comms.begin(); comm != active_comms.end(); comm++) {
    if (*comm == MPI_COMM_WORLD
        || *comm == MPI_COMM_SELF
        || *comm == MPI_COMM_NULL) {
      continue;
    }
    MPI_Group group;
    MPI_Comm_group(*comm, &group);
    comm_groups[*comm] = getGroupMembers(group);
  }
}

void
restoreCommGroups()
{
  MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);
  MPI_Group realWorldGroup = VIRTUAL_TO_REAL_GROUP(worldGroup);
  std::unordered_map<MPI_Comm, group_members_t*>::iterator it;
  for (it = comm_groups.begin(); it != comm_groups.end(); it++) {
    MPI_Group newgroup;
    MPI_Comm newcomm;
    MPI_Comm oldcomm = it->first;
    group_members_t *members = it->second;

    // Recreate the group according to recorded metadata of the
    // communicator and the group behind it.
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    NEXT_FUNC(Group_incl)(realWorldGroup, members->size, members->world_ranks,
                         &newgroup);
    NEXT_FUNC(Comm_create_group)(MPI_COMM_WORLD, newgroup, 0, &newcomm);
    RETURN_TO_UPPER_HALF();

    // if the newly created group is not one of active groups before checkpoint,
    // free the group. Otherwise, update the virtual group id with the new
    // group id.
    if (active_groups.find(members->group_id) == active_groups.end()) {
      JUMP_TO_LOWER_HALF(lh_info.fsaddr);
      NEXT_FUNC(Group_free)(&newgroup);
      RETURN_TO_UPPER_HALF();
    } else {
      UPDATE_GROUP_MAP(members->group_id, newgroup);
    }
    UPDATE_COMM_MAP(oldcomm, newcomm);
  }
  comm_groups.clear();
}
