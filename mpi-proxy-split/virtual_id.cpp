#include <unordered_map>
#include <mpi.h>

#include "virtual_id.h"
#include "switch_context.h"
#include "mpi_nextfunc.h"

MPI_Group g_world_group;
std::unordered_map<int, virt_id_entry*> virt_ids;
std::unordered_map<int, mana_ggid_desc*> ggid_table;

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
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_group)(real_comm, &local_group);
  RETURN_TO_UPPER_HALF();
  desc->group = new_virt_group(local_group);
  desc->group_desc = (mana_group_desc*)get_virt_id_desc({.group = desc->group});

  int ggid = generate_ggid(desc->group_desc->global_ranks,
                           desc->group_desc->size);
  desc->ggid = ggid;
  std::unordered_map<int, mana_ggid_desc*>::iterator it = ggid_table.find(ggid);
  if (it != ggid_table.end()) {
    mana_ggid_desc *ggid_desc = (mana_ggid_desc*)malloc(sizeof(mana_ggid_desc));
    ggid_desc->seq_num = 0;
    ggid_desc->target_num = 0;
    ggid_table[ggid] = ggid_desc;

    desc->ggid_desc = ggid_desc;
  } else {
    desc->ggid_desc = it->second;
  }

  mana_handle virt_id;
  virt_id = add_virt_id({.comm = real_comm}, desc, MANA_COMM_KIND);
  return virt_id.comm;
}

MPI_Group new_virt_group(MPI_Group real_group) {
  int *local_ranks;
  mana_group_desc *desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));

  desc->ref_count = 1;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_size)(real_group, &desc->size);
  NEXT_FUNC(Group_rank)(real_group, &desc->rank);
  local_ranks = (int*)malloc(sizeof(int) * desc->size);

  desc->global_ranks = (int*)malloc(sizeof(int) * desc->size);
  for (int i = 0; i < desc->size; i++) {
    local_ranks[i] = i;
  }
  NEXT_FUNC(Group_translate_ranks)(real_group, desc->size, local_ranks,
                                   g_world_group, desc->global_ranks);
  RETURN_TO_UPPER_HALF();

  mana_handle virt_id;
  virt_id = add_virt_id({.group = real_group}, desc, MANA_GROUP_KIND);
  return virt_id.group;
}

MPI_Request new_virt_req(MPI_Request real_request) {
  mana_request_desc *desc = (mana_request_desc*)malloc(sizeof(mana_request_desc));
  // See comment in request_desc definition.
  mana_handle virt_id;
  virt_id = add_virt_id({.request = real_request}, NULL, MANA_REQUEST_KIND);
  return virt_id.request;
}

MPI_Op new_virt_op(MPI_Op real_op) {
  mana_op_desc *desc = (mana_op_desc*)malloc(sizeof(mana_op_desc));
  // MPI_User_function and commute are available at create time.
  // So this function only creates the descriptor for the real_op,
  // and let the MPI_Op_create wrapper to fill-in these two fields
  // in the descriptor.

  mana_handle virt_id;
  virt_id = add_virt_id({.op = real_op}, desc, MANA_OP_KIND);
  return virt_id.op;
}

MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype) {
  mana_datatype_desc *desc = (mana_datatype_desc*)
                             malloc(sizeof(mana_datatype_desc));
  mana_handle virt_id;
  virt_id = add_virt_id({.datatype = real_datatype}, desc, MANA_DATATYPE_KIND);
  return virt_id.datatype;
}

MPI_File new_virt_file(MPI_File real_file) {
  mana_handle virt_id;
  virt_id = add_virt_id({.file = real_file}, NULL, NULL);
  return virt_id.file;
}

void reconstruct_comm_desc(virt_id_entry *entry) {
  MPI_Group group;
  mana_comm_desc *comm_desc = (mana_comm_desc*)entry->desc;
  mana_group_desc *group_desc = comm_desc->group_desc;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, group_desc->size, group_desc->global_ranks, &group);
  NEXT_FUNC(Comm_create_group)(lh_info->MANA_COMM_WORLD, group, 0, &(entry->real_id.group));
  NEXT_FUNC(Group_free)(&group);
  RETURN_TO_UPPER_HALF();
}

void reconstruct_group_desc(virt_id_entry *entry) {
  mana_group_desc *desc = (mana_group_desc*)entry->desc;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->global_ranks,
                        &(entry->real_id.group));
  RETURN_TO_UPPER_HALF();
}

void reconstruct_op_desc(virt_id_entry *entry) {
  mana_op_desc *desc = (mana_op_desc*)entry->desc;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Op_create)(desc->user_fn, desc->commute, &(entry->real_id.group));
  RETURN_TO_UPPER_HALF();
}

void reconstruct_descriptors() {
  for (virt_id_iterator it = virt_ids.begin(); it != virt_ids.end(); it++) {
    int kind = it->first >> MANA_VIRT_ID_KIND_SHIFT;
    virt_id_entry *entry = it->second;
    switch (kind) {
      case MANA_COMM_KIND:
        reconstruct_comm_desc(entry);
        break;
      case MANA_GROUP_KIND:
        reconstruct_group_desc(entry);
        break;
      case MANA_OP_KIND:
        reconstruct_op_desc(entry);
        break;
      case MANA_DATATYPE_KIND:
        // Handled by the record-and-replay algorithm.
        break;
      case MANA_REQUEST_KIND:
        // Handled separately by P2P wrappers and CC algorithm.
        break;
      case MANA_FILE_KIND:
        // Handled by other parts of MANA. Can be unified in this
        // function in the future.
        break;
      default:
        fprintf(stderr, "Unknown MANA handle type detected\n");
        abort();
        break;
    }
  }
}
