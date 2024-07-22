#include <unordered_map>
#include <mpi.h>

#include "virtual_id.h"
#include "switch_context.h"

std::unordered_map<int, virt_id_entry*> virt_ids;

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
inline int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

int generate_ggid(int *ranks, int size) {
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

  // Cache the group of the communicator
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  NEXT_FUNC(Comm_group)(real_comm, &local_group);
  RETURN_TO_UPPER_HALF();
  desc->group = new_virt_group(local_group);
  desc->group_desc = get_virt_id_desc(virt_ids, desc->group);

  int ggid = generate_ggid(desc->group_desc->global_ranks,
                           desc->group_desc->size);
  std::unordered_map<int, mana_ggid_desc*>::iterator it = ggid_table.find(ggid);
  if (it != ggid.end()) {
    mana_ggid_desc *ggid_desc = malloc(sizeof(mana_comm_ggid));
    ggid_desc->seq_num = 0;
    ggid_desc->target_num = 0;
    ggid_table[ggid] = ggid_desc;

    desc->ggid_desc = ggid_desc;
  } else {
    desc->ggid_desc = it.second;
  }

  mana_handle real_id, virt_id;
  real_id.comm = real_comm;
  virt_id = add_virt_id(real_id, desc, COMM_KIND);

  return virt_id.comm;
}

MPI_Group new_virt_group(MPI_Group real_group) {
  int *local_ranks;
  mana_group_desc *desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));

  desc->ref_count = 1;
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

  mana_handle real_id, virt_id;
  real_id.group = real_group;
  virt_id = add_virt_id(real_id, desc, GROUP_KIND);

  return virt_id.group;
}

MPI_Request new_virt_req(MPI_Request real_request) {
  mana_request_desc *desc = (mana_request_desc*)malloc(sizeof(mana_request_desc));
  // See comment in request_desc definition.
  mana_handle real_id, virt_id;
  real_id.request = real_request;
  virt_id = add_virt_id(real_req, NULL, REQUEST_KIND);

  return virt_id.request;
}

MPI_Op new_virt_op(MPI_Op real_op) {
  mana_op_desc *desc = (mana_op_desc*)malloc(sizeof(mana_op_desc));
  // MPI_User_function and commute are available at create time.
  // So this function only creates the descriptor for the real_op,
  // and let the MPI_Op_create wrapper to fill-in these two fields
  // in the descriptor.

  mana_handle real_id, virt_id;
  real_id.op = real_op;
  virt_id = add_virt_id(real_op, desc, OP_KIND);

  return virt_id.op;
}

MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype) {
  mana_datatype_desc *desc = (mana_datatype_desc*)
                             malloc(sizeof(mana_datatype_desc));
  // TODO: Fill Datatype information in the struct

  mana_handle real_id, virt_id;
  real_id.datatype = real_datatype;
  virt_id = add_virt_id(real_datatype, desc, DATATYPE_KIND);

  return virt_id.datatype;
}

// TODO: Do we need take special care of these two handle types?
MPI_File new_virt_file(MPI_File real_file) {
  mana_handle real_id, virt_id;
  real_id.file = real_file;
  virt_id = add_virt_id(real_file, NULL, NULL);
  return virt_id.file;
}
