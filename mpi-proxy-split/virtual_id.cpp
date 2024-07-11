#include <unordered_map>
#include <mpi.h>

#include "virtual_id.h"
#include "switch_context.h"

virt_id_table virt_ids;

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
inline int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

int get_ggid(int *ranks, int size) {
  int ggid = 0;
  for (int i = 0; i < size; i++) {
    ggid ^= hash(ranks[i] + 1);
  }
  return ggid;
}

MPI_Comm new_virt_comm(MPI_Comm real_comm) {
  MPI_Group local_group;
  int *local_ranks;
  mana_comm_desc *desc = (mana_comm_desc*)malloc(sizeof(mana_comm_desc));

  desc->real_id = real_comm;
  // Cache the group of the communicator
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(real_comm, &local_group);
  RETURN_TO_UPPER_HALF();
  desc->group = new_virt_group(local_group);
  desc->group_desc = get_virt_id_desc(virt_ids, desc->group);

  desc->ggid = generate_ggid(desc->group_desc->global_ranks,
                             desc->group_desc->size);
  desc->seq_num = 0;
  desc->target_num = 0;

  return (MPI_Comm)add_virt_id(virt_ids, real_comm, desc, COMM_MASK);
}

MPI_Group new_virt_group(MPI_Group real_group) {
  int *local_ranks;
  mana_group_desc *desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));

  desc->real_id = real_group;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Group_size)(real_comm, &desc->size);
  NEXT_FUNC(Group_rank)(real_comm, &desc->rank);
  local_ranks = malloc(sizeof(int) * desc->size);

  desc->global_ranks = malloc(sizeof(int) * desc->size);
  for (int i = 0; i < desc->size) {
    local_ranks[i] = i;
  }
  NEXT_FUNC(Group_translate_ranks)(real_group, desc->size, local_ranks,
                                   g_world_group, desc->global_ranks);

  NEXT_FUNC(Group_free)(local_group);
  RETURN_TO_UPPER_HALF();

  return (MPI_Comm)add_virt_id(virt_ids, real_group, desc, GROUP_MASK);
}

MPI_Request new_virt_req(MPI_Request real_req) {
  mana_request_desc *desc = (mana_request_desc*)malloc(sizeof(mana_request_desc));
  // See comment in request_desc definition.
  return (MPI_Comm)add_virt_id(virt_ids, real_req, NULL, REQUEST_MASK);
}

MPI_Op new_virt_op(MPI_Op real_op) {
  mana_op_desc *desc = (mana_op_desc*)malloc(sizeof(mana_op_desc));
  // MPI_User_function and commute are available at create time.
  // So this function only creates the descriptor for the real_op,
  // and let the MPI_Op_create wrapper to fill-in these two fields
  // in the descriptor.
  return (MPI_Comm)add_virt_id(virt_ids, real_op, desc, OP_MASK);
}

MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype) {
  mana_datatype_desc *desc = (mana_datatype_desc*)
                             malloc(sizeof(mana_datatype_desc));
  desc->real_id = real_datatype;
  // TODO: Fill Datatype information in the struct

  return (MPI_Comm)add_virt_id(virt_ids, real_datatype, desc, DATATYPE_MASK);
}

