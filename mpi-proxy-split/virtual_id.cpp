#include <mpi.h>

#include "virtual_id.h"
#include "switch_context.h"
#include "mpi_nextfunc.h"

MPI_Group g_world_group;
std::map<int, virt_id_entry*> virt_ids;
std::map<int, mana_ggid_desc*> ggid_table;

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
int hash(int i) {
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
  if (real_comm == MPI_COMM_NULL) {
    return MPI_COMM_NULL;
  }
  MPI_Group local_group;
  int *local_ranks;
  mana_comm_desc *desc = (mana_comm_desc*)malloc(sizeof(mana_comm_desc));

  // Cache the group of the communicator
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_group)(real_comm, &local_group);
  RETURN_TO_UPPER_HALF();
  desc->group = new_virt_group(local_group);
  desc->group_desc = (mana_group_desc*)get_virt_id_desc({.group = desc->group});

  desc->ggid = generate_ggid(desc->group_desc->global_ranks,
                           desc->group_desc->size);
  std::map<int, mana_ggid_desc*>::iterator it = ggid_table.find(desc->ggid);
  if (it == ggid_table.end()) {
    desc->ggid_desc = (mana_ggid_desc*)malloc(sizeof(mana_ggid_desc));
    desc->ggid_desc->seq_num = 0;
    desc->ggid_desc->target_num = 0;
    ggid_table[desc->ggid] = desc->ggid_desc;
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
  RETURN_TO_UPPER_HALF();
  local_ranks = (int*)malloc(sizeof(int) * desc->size);

  desc->global_ranks = (int*)malloc(sizeof(int) * desc->size);
  for (int i = 0; i < desc->size; i++) {
    local_ranks[i] = i;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
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
  virt_id = add_virt_id({.file = real_file}, NULL, MANA_FILE_KIND);
  return virt_id.file;
}

void reconstruct_comm_desc(virt_id_entry *entry) {
  MPI_Group group;
  mana_comm_desc *comm_desc = (mana_comm_desc*)entry->desc;
  mana_group_desc *group_desc = comm_desc->group_desc;
  printf("comm %x rank %d size %d:", comm_desc->ggid, group_desc->rank, group_desc->size);
  for (int i = 0; i < group_desc->size; i++) {
    printf(" %d,", group_desc->global_ranks[i]);
  }
  printf("\n");
  fflush(stdout);
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, group_desc->size, group_desc->global_ranks, &group);
  NEXT_FUNC(Comm_create_group)(lh_info->MANA_COMM_WORLD, group, 0, &(entry->real_id.comm));
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
  NEXT_FUNC(Op_create)(desc->user_fn, desc->commute, &(entry->real_id.op));
  RETURN_TO_UPPER_HALF();
}

void reconstruct_descriptors() {
  for (virt_id_iterator it = virt_ids.begin(); it != virt_ids.end(); it++) {
    // Don't reconstruct predefiend objects.
    if (is_predefined_id({._handle = it->first})) {
      continue;
    }
    int kind = it->first >> MANA_VIRT_ID_KIND_SHIFT;
    virt_id_entry *entry = it->second;
    switch (kind) {
      case MANA_COMM_KIND:
        printf("Process %d is reconstructing comm(virtual) %x\n", g_world_rank, it->first);
        fflush(stdout);
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

void init_predefined_virt_ids() {
  // Initialize g_world_comm and g_world_group.
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_dup)(lh_info->MANA_COMM_WORLD, &g_world_comm);
  NEXT_FUNC(Comm_group)(lh_info->MANA_COMM_WORLD, &g_world_group);
  RETURN_TO_UPPER_HALF();
  g_world_comm = new_virt_comm(g_world_comm);

  // MPI_COMM_WORLD
  mana_comm_desc *comm_world_desc = (mana_comm_desc*)malloc(sizeof(mana_comm_desc));
  comm_world_desc->group = g_world_group;
  comm_world_desc->group_desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_size)(g_world_group, &comm_world_desc->group_desc->size);
  NEXT_FUNC(Group_rank)(g_world_group, &comm_world_desc->group_desc->rank);
  RETURN_TO_UPPER_HALF();
  comm_world_desc->group_desc->global_ranks = (int*)malloc(sizeof(int) * comm_world_desc->group_desc->size);
  for (int i = 0; i < comm_world_desc->group_desc->size; i++) {
    comm_world_desc->group_desc->global_ranks[i] = i;
  }

  comm_world_desc->ggid = generate_ggid(comm_world_desc->group_desc->global_ranks,
                                        comm_world_desc->group_desc->size);
  comm_world_desc->ggid_desc = (mana_ggid_desc*)malloc(sizeof(mana_ggid_desc));
  comm_world_desc->ggid_desc->seq_num = 0;
  comm_world_desc->ggid_desc->target_num = 0;
  ggid_table[comm_world_desc->ggid] = comm_world_desc->ggid_desc;

  virt_id_entry *comm_world_entry = (virt_id_entry*)malloc(sizeof(virt_id_entry));
  comm_world_entry->real_id = {.comm = lh_info->MANA_COMM_WORLD};
  comm_world_entry->desc = comm_world_desc;
  virt_ids[MPI_COMM_WORLD] = comm_world_entry;

  // Group of MPI_COMM_WORLD
  virt_id_entry *group_world_entry = (virt_id_entry*)malloc(sizeof(virt_id_entry));
  group_world_entry->real_id = {.group = g_world_group};
  group_world_entry->desc = comm_world_desc->group_desc;
  virt_ids[g_world_group] = group_world_entry;

  // Group of MPI_COMM_SELF
  MPI_Group self_group;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_group)(lh_info->MANA_COMM_SELF, &self_group);
  RETURN_TO_UPPER_HALF();
  self_group = new_virt_group(self_group);

  // MPI_COMM_WORLD
  mana_comm_desc *comm_self_desc = (mana_comm_desc*)malloc(sizeof(mana_comm_desc));
  comm_self_desc->group = self_group;
  comm_self_desc->group_desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));
  comm_self_desc->group_desc->size = 1;
  comm_self_desc->group_desc->rank = 0;
  comm_self_desc->group_desc->global_ranks = (int*)malloc(sizeof(int));
  comm_self_desc->group_desc->global_ranks[0] = 0;

  comm_self_desc->ggid = generate_ggid(comm_self_desc->group_desc->global_ranks,
                                       comm_self_desc->group_desc->size);
  comm_self_desc->ggid_desc = (mana_ggid_desc*)malloc(sizeof(mana_ggid_desc));
  comm_self_desc->ggid_desc->seq_num = 0;
  comm_self_desc->ggid_desc->target_num = 0;

  virt_id_entry *comm_self_entry = (virt_id_entry*)malloc(sizeof(virt_id_entry));
  comm_self_entry->real_id = {.comm = lh_info->MANA_COMM_SELF};
  comm_self_entry->desc = comm_self_desc;
  virt_ids[MPI_COMM_SELF] = comm_self_entry;

#if 0
  // MPI_GROUP_NULL
  virt_id_entry *group_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  group_null_entry->real_id = {.group = lh_info->MPI_GROUP_NULL};
  group_null_entry->desc = NULL;
  virt_ids[MPI_GROUP_NULL] = group_null_entry;

  // MPI_COMM_NULL
  virt_id_entry *comm_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  comm_null_entry->real_id = {.comm = lh_info->MPI_COMM_NULL};
  comm_null_entry->desc = NULL;
  virt_ids[MPI_COMM_NULL] = comm_null_entry;

  // MPI_DATATYPE_NULL
  virt_id_entry *datatype_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  datatype_null_entry->real_id = {.datatype = lh_info->MPI_DATATYPE_NULL};
  datatype_null_entry->desc = NULL;
  virt_ids[MPI_DATATYPE_NULL] = datatype_null_entry;

  // MPI_REQUEST_NULL
  virt_id_entry *request_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  request_null_entry->real_id = {.request = lh_info->MPI_REQUEST_NULL};
  request_null_entry->desc = NULL;
  virt_ids[MPI_REQUEST_NULL] = request_null_entry;

  // MPI_OP_NULL
  virt_id_entry *op_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  op_null_entry->real_id = {.op = lh_info->MPI_OP_NULL};
  op_null_entry->desc = NULL;
  virt_ids[MPI_OP_NULL] = op_null_entry;

  // MPI_FIle_NULL
  virt_id_entry *file_null_entry = (virt_id_entry*)malloc(sizeof(entry));
  file_null_entry->real_id = {.file = lh_info->MPI_FILE_NULL};
  file_null_entry->desc = NULL;
  virt_ids[MPI_FILE_NULL] = file_null_entry;
#endif
}

int is_predefined_id(mana_handle id) {
  if (id.comm == MPI_COMM_WORLD ||
      id.comm == MPI_COMM_SELF ||
      id.comm == MPI_COMM_NULL ||
      id.comm == g_world_comm ||
      id.group == MPI_GROUP_NULL ||
      id.group == g_world_group ||
      id.datatype == MPI_DATATYPE_NULL ||
      id.request == MPI_REQUEST_NULL ||
      id.op == MPI_OP_NULL ||
      id.file == MPI_FILE_NULL) {
    return 1;
  } else {
    return 0;
  }
}

mana_handle add_virt_id(mana_handle real_id, void *desc, int kind) {
  static int next_id = 0;
  mana_handle new_virt_id;
  do {
    new_virt_id._handle = kind << MANA_VIRT_ID_KIND_SHIFT;
    new_virt_id._handle = new_virt_id._handle | next_id;
    next_id++;
  } while (is_predefined_id({._handle = new_virt_id._handle}));

  virt_id_entry *entry = (virt_id_entry*)malloc(sizeof(entry));
  entry->real_id = real_id;
  entry->desc = desc;
  virt_ids[new_virt_id._handle] = entry;

  return new_virt_id;
}

#define DEBUG_VIRTID
virt_id_entry* get_virt_id_entry(mana_handle virt_id) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    // If it's not in the virt_id, table, check if it's a predefined
    // handle (MPI constant).
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
#ifdef DEBUG_VIRTID
    volatile int dummy = 1;
    while (dummy);
#else
    abort();
#endif
  }
  return it->second;
}

// Functions convert from virtual id to real id and functions test and
// convert lower half MPI constants to upper half constants
MPI_Comm REAL_COMM(MPI_Comm comm) {
  // Test if comm is predefined communicator
  if (comm == MPI_COMM_NULL) {
    return lh_info->MANA_COMM_NULL;
  } else if (comm == MPI_COMM_WORLD) {
    return lh_info->MANA_COMM_WORLD;
  } else if (comm == MPI_COMM_SELF) {
    return lh_info->MANA_COMM_SELF;
  } else { // Not predefiend
    return get_real_id({.comm == comm}).comm;
  }
}

MPI_Comm OUTPUT_COMM(MPI_Comm comm) {
  // Test if comm is predefined communicator
  if (comm == lh_info->MANA_COMM_NULL) {
    return MPI_COMM_NULL;
  } else if (comm == lh_info->MANA_COMM_WORLD) {
    return MPI_COMM_WORLD;
  } else if (comm == lh_info->MANA_COMM_SELF) {
    return MPI_COMM_SELF;
  } else { // Not predefiend
    return new_virt_comm(comm);
  }
}

MPI_Group REAL_GROUP(MPI_Group group) {
  // Test if group is predefined
  if (group == MPI_GROUP_NULL) {
    return lh_info->MANA_GROUP_NULL;
  } else if (group == MPI_GROUP_EMPTY) {
    return lh_info->MANA_GROUP_EMPTY;
  } else { // Not predefined
    return get_real_id({.group = group}).group;
  }
}

MPI_Group OUTPUT_GROUP(MPI_Group group) {
  // Test if group is predefined
  if (group == lh_info->MANA_GROUP_NULL) {
    return MPI_GROUP_NULL;
  } else if (group == lh_info->MANA_GROUP_EMPTY) {
    return MPI_GROUP_EMPTY;
  } else { // Not predefined
    return new_virt_group(group);
  }
}

mana_handle get_real_id(mana_handle virt_id) {
  return get_virt_id_entry(virt_id)->real_id;
}

void* get_virt_id_desc(mana_handle virt_id) {
  return get_virt_id_entry(virt_id)->desc;
}

void free_desc(void *desc, int kind) {
  if (kind == MANA_COMM_KIND) {
    mana_comm_desc *comm_desc = (mana_comm_desc*)desc;
    free_desc(comm_desc->group_desc, MANA_GROUP_KIND);
  } else if (kind == MANA_GROUP_KIND) {
    mana_group_desc *group_desc = (mana_group_desc*)desc;
    free(group_desc->global_ranks);
  }
  free(desc);
}

void free_virt_id(mana_handle virt_id) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  int kind = virt_id._handle >> MANA_VIRT_ID_KIND_SHIFT;
  free_desc(it->second->desc, kind); // free descriptor
  free(it->second); // free virt_id_entry
}

void update_virt_id(mana_handle virt_id, mana_handle real_id) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  virt_id_entry *entry = it->second;
  entry->real_id = real_id;
}

