#include <mpi.h>
#include <algorithm>
#include <vector>

#include "virtual_id.h"
#include "switch-context.h"
#include "mpi_nextfunc.h"
#include "seq_num.h"
#include <iostream>

MPI_Group g_world_group;
std::map<int64_t, int64_t> upper_to_lower_constants;
std::map<int64_t, int64_t> lower_to_upper_constants;

// The virtual-ID table.  See virtual_id.h for the handle layout and for the
// synchronization it relies on.
virt_id_entry *virt_id_chunks[MANA_VIRT_ID_NUM_CHUNKS];
static int virt_id_free_head = -1;   // First slot of the free list, or -1
static int virt_id_high_water = 0;   // Slots [0, high_water) have been used
static uint64_t virt_id_next_seq = 0;
static size_t virt_id_live = 0;

static inline virt_id_entry*
slot_entry(int slot)
{
  return &virt_id_chunks[slot >> MANA_VIRT_ID_CHUNK_SHIFT]
                        [slot & (MANA_VIRT_ID_CHUNK_SIZE - 1)];
}

// Hash function on integers. Consult https://stackoverflow.com/questions/664014/.
// Returns a hash.
int hash(int i) {
  return i * 2654435761 % ((unsigned long)1 << 32);
}

unsigned int generate_ggid(int *ranks, int size) {
  unsigned int ggid = 0;
  for (int i = 0; i < size; i++) {
    ggid ^= hash(ranks[i] + 1);
  }
  return ggid;
}

int is_predefined_id(mana_mpi_handle id) {
  std::map<int64_t, int64_t>::iterator it;
  it = upper_to_lower_constants.find(id._handle64);
  return it != upper_to_lower_constants.end();
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
  NEXT_FUNC(Comm_size)(real_comm, &desc->size);
  NEXT_FUNC(Comm_rank)(real_comm, &desc->rank);
  NEXT_FUNC(Comm_group)(real_comm, &local_group);
  RETURN_TO_UPPER_HALF();

  local_ranks = (int*)malloc(sizeof(int) * desc->size);
  desc->global_ranks = (int*)malloc(sizeof(int) * desc->size);
  for (int i = 0; i < desc->size; i++) {
    local_ranks[i] = i;
  }
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_translate_ranks)(local_group, desc->size, local_ranks,
                                   g_world_group, desc->global_ranks);
  RETURN_TO_UPPER_HALF();

  unsigned int ggid = generate_ggid(desc->global_ranks,
                                    desc->size);
  seq_num[ggid] = 0;
  target[ggid] = 0;
  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.comm = real_comm}, desc, MANA_COMM_KIND);
  ggid_table[virt_id.comm] = ggid;
  return virt_id.comm;
}

MPI_Group new_virt_group(MPI_Group real_group) {
  int *local_ranks;
  mana_group_desc *desc = (mana_group_desc*)malloc(sizeof(mana_group_desc));

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

  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.group = real_group}, desc, MANA_GROUP_KIND);
  return virt_id.group;
}

MPI_Request new_virt_request(MPI_Request real_request) {
  // See comment in request_desc definition.
  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.request = real_request}, NULL, MANA_REQUEST_KIND);
  return virt_id.request;
}

MPI_Op new_virt_op(MPI_Op real_op) {
  mana_op_desc *desc = (mana_op_desc*)malloc(sizeof(mana_op_desc));
  // MPI_User_function and commute are available at create time.
  // So this function only creates the descriptor for the real_op,
  // and let the MPI_Op_create wrapper to fill-in these two fields
  // in the descriptor.

  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.op = real_op}, desc, MANA_OP_KIND);
  return virt_id.op;
}

MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype) {
  mana_datatype_desc *desc = (mana_datatype_desc*)
                             malloc(sizeof(mana_datatype_desc));
  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.datatype = real_datatype}, desc, MANA_DATATYPE_KIND);
  return virt_id.datatype;
}

MPI_File new_virt_file(MPI_File real_file) {
  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.file = real_file}, NULL, MANA_FILE_KIND);
  return virt_id.file;
}

void reconstruct_comm_desc(virt_id_entry *entry) {
  MPI_Group group;
  mana_comm_desc *desc = (mana_comm_desc*)entry->desc;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->global_ranks, &group);
  NEXT_FUNC(Comm_create_group)(lh_info->MANA_COMM_WORLD, group, 0, &(entry->real_id.comm));
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
  NEXT_FUNC(Op_create)(desc->user_fn, desc->commute, &(entry->real_id.op));
  RETURN_TO_UPPER_HALF();
}

void reconstruct_descriptors() {
  // Reconstruct by kind, then in creation order.  MPI_Comm_create_group is
  // collective, so all ranks must reconstruct their communicators in the
  // same order.  Slots are reused, so the slot order is not the creation
  // order.
  std::vector<virt_id_entry*> entries;
  for (int slot = 0; slot < virt_id_high_water; slot++) {
    virt_id_entry *entry = slot_entry(slot);
    if (entry->virt != 0) {
      entries.push_back(entry);
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const virt_id_entry *a, const virt_id_entry *b) {
              int kind_a = a->virt >> MANA_VIRT_ID_KIND_SHIFT;
              int kind_b = b->virt >> MANA_VIRT_ID_KIND_SHIFT;
              return kind_a != kind_b ? kind_a < kind_b : a->seq < b->seq;
            });
  for (virt_id_entry *entry : entries) {
    // Predefined objects are never in the table: add_virt_id() doesn't
    // create handles equal to them, so there's nothing to skip here.
    int kind = entry->virt >> MANA_VIRT_ID_KIND_SHIFT;
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

void init_predefined_virt_ids() {
  // Fill in the upper-to-lower mapping
  upper_to_lower_constants[(int64_t)MPI_GROUP_NULL] = (int64_t)lh_info->MANA_GROUP_NULL;
  upper_to_lower_constants[(int64_t)MPI_COMM_NULL] = (int64_t)lh_info->MANA_COMM_NULL;
  upper_to_lower_constants[(int64_t)MPI_COMM_WORLD] = (int64_t)lh_info->MANA_COMM_WORLD;
  upper_to_lower_constants[(int64_t)MPI_COMM_SELF] = (int64_t)lh_info->MANA_COMM_SELF;
  upper_to_lower_constants[(int64_t)MPI_REQUEST_NULL] = (int64_t)lh_info->MANA_REQUEST_NULL;
  upper_to_lower_constants[(int64_t)MPI_MESSAGE_NULL] = (int64_t)lh_info->MANA_MESSAGE_NULL;
  upper_to_lower_constants[(int64_t)MPI_OP_NULL] = (int64_t)lh_info->MANA_OP_NULL;
  upper_to_lower_constants[(int64_t)MPI_ERRHANDLER_NULL] = (int64_t)lh_info->MANA_ERRHANDLER_NULL;
  upper_to_lower_constants[(int64_t)MPI_INFO_NULL] = (int64_t)lh_info->MANA_INFO_NULL;
  upper_to_lower_constants[(int64_t)MPI_WIN_NULL] = (int64_t)lh_info->MANA_WIN_NULL;
  upper_to_lower_constants[(int64_t)MPI_FILE_NULL] = (int64_t)lh_info->MANA_FILE_NULL;
  upper_to_lower_constants[(int64_t)MPI_INFO_ENV] = (int64_t)lh_info->MANA_INFO_ENV;
  upper_to_lower_constants[(int64_t)MPI_GROUP_EMPTY] = (int64_t)lh_info->MANA_GROUP_EMPTY;
  upper_to_lower_constants[(int64_t)MPI_MESSAGE_NO_PROC] = (int64_t)lh_info->MANA_MESSAGE_NO_PROC;
  upper_to_lower_constants[(int64_t)MPI_MAX] = (int64_t)lh_info->MANA_MAX;
  upper_to_lower_constants[(int64_t)MPI_MIN] = (int64_t)lh_info->MANA_MIN;
  upper_to_lower_constants[(int64_t)MPI_SUM] = (int64_t)lh_info->MANA_SUM;
  upper_to_lower_constants[(int64_t)MPI_PROD] = (int64_t)lh_info->MANA_PROD;
  upper_to_lower_constants[(int64_t)MPI_LAND] = (int64_t)lh_info->MANA_LAND;
  upper_to_lower_constants[(int64_t)MPI_BAND] = (int64_t)lh_info->MANA_BAND;
  upper_to_lower_constants[(int64_t)MPI_LOR] = (int64_t)lh_info->MANA_LOR;
  upper_to_lower_constants[(int64_t)MPI_BOR] = (int64_t)lh_info->MANA_BOR;
  upper_to_lower_constants[(int64_t)MPI_LXOR] = (int64_t)lh_info->MANA_LXOR;
  upper_to_lower_constants[(int64_t)MPI_BXOR] = (int64_t)lh_info->MANA_BXOR;
  upper_to_lower_constants[(int64_t)MPI_MAXLOC] = (int64_t)lh_info->MANA_MAXLOC;
  upper_to_lower_constants[(int64_t)MPI_MINLOC] = (int64_t)lh_info->MANA_MINLOC;
  upper_to_lower_constants[(int64_t)MPI_REPLACE] = (int64_t)lh_info->MANA_REPLACE;
  upper_to_lower_constants[(int64_t)MPI_NO_OP] = (int64_t)lh_info->MANA_NO_OP;
  upper_to_lower_constants[(int64_t)MPI_DATATYPE_NULL] = (int64_t)lh_info->MANA_DATATYPE_NULL;
  upper_to_lower_constants[(int64_t)MPI_BYTE] = (int64_t)lh_info->MANA_BYTE;
  upper_to_lower_constants[(int64_t)MPI_PACKED] = (int64_t)lh_info->MANA_PACKED;
  upper_to_lower_constants[(int64_t)MPI_CHAR] = (int64_t)lh_info->MANA_CHAR;
  upper_to_lower_constants[(int64_t)MPI_SHORT] = (int64_t)lh_info->MANA_SHORT;
  upper_to_lower_constants[(int64_t)MPI_INT] = (int64_t)lh_info->MANA_INT;
  upper_to_lower_constants[(int64_t)MPI_LONG] = (int64_t)lh_info->MANA_LONG;
  upper_to_lower_constants[(int64_t)MPI_FLOAT] = (int64_t)lh_info->MANA_FLOAT;
  upper_to_lower_constants[(int64_t)MPI_DOUBLE] = (int64_t)lh_info->MANA_DOUBLE;
  upper_to_lower_constants[(int64_t)MPI_LONG_DOUBLE] = (int64_t)lh_info->MANA_LONG_DOUBLE;
  upper_to_lower_constants[(int64_t)MPI_UNSIGNED_CHAR] = (int64_t)lh_info->MANA_UNSIGNED_CHAR;
  upper_to_lower_constants[(int64_t)MPI_SIGNED_CHAR] = (int64_t)lh_info->MANA_SIGNED_CHAR;
  upper_to_lower_constants[(int64_t)MPI_UNSIGNED_SHORT] = (int64_t)lh_info->MANA_UNSIGNED_SHORT;
  upper_to_lower_constants[(int64_t)MPI_UNSIGNED_LONG] = (int64_t)lh_info->MANA_UNSIGNED_LONG;
  upper_to_lower_constants[(int64_t)MPI_UNSIGNED] = (int64_t)lh_info->MANA_UNSIGNED;
  upper_to_lower_constants[(int64_t)MPI_FLOAT_INT] = (int64_t)lh_info->MANA_FLOAT_INT;
  upper_to_lower_constants[(int64_t)MPI_DOUBLE_INT] = (int64_t)lh_info->MANA_DOUBLE_INT;
  upper_to_lower_constants[(int64_t)MPI_LONG_DOUBLE_INT] = (int64_t)lh_info->MANA_LONG_DOUBLE_INT;
  upper_to_lower_constants[(int64_t)MPI_LONG_INT] = (int64_t)lh_info->MANA_LONG_INT;
  upper_to_lower_constants[(int64_t)MPI_SHORT_INT] = (int64_t)lh_info->MANA_SHORT_INT;
  upper_to_lower_constants[(int64_t)MPI_2INT] = (int64_t)lh_info->MANA_2INT;
  upper_to_lower_constants[(int64_t)MPI_WCHAR] = (int64_t)lh_info->MANA_WCHAR;
  upper_to_lower_constants[(int64_t)MPI_LONG_LONG_INT] = (int64_t)lh_info->MANA_LONG_LONG_INT;
  upper_to_lower_constants[(int64_t)MPI_LONG_LONG] = (int64_t)lh_info->MANA_LONG_LONG;
  upper_to_lower_constants[(int64_t)MPI_UNSIGNED_LONG_LONG] = (int64_t)lh_info->MANA_UNSIGNED_LONG_LONG;
#if 0
  upper_to_lower_constants[(int64_t)MPI_2COMPLEX] = (int64_t)lh_info->MANA_2COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_CXX_COMPLEX] = (int64_t)lh_info->MANA_CXX_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_2DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_2DOUBLE_COMPLEX;
#endif
  upper_to_lower_constants[(int64_t)MPI_CHARACTER] = (int64_t)lh_info->MANA_CHARACTER;
  upper_to_lower_constants[(int64_t)MPI_LOGICAL] = (int64_t)lh_info->MANA_LOGICAL;
#if 0
  upper_to_lower_constants[(int64_t)MPI_LOGICAL1] = (int64_t)lh_info->MANA_LOGICAL1;
  upper_to_lower_constants[(int64_t)MPI_LOGICAL2] = (int64_t)lh_info->MANA_LOGICAL2;
  upper_to_lower_constants[(int64_t)MPI_LOGICAL4] = (int64_t)lh_info->MANA_LOGICAL4;
  upper_to_lower_constants[(int64_t)MPI_LOGICAL8] = (int64_t)lh_info->MANA_LOGICAL8;
#endif
  upper_to_lower_constants[(int64_t)MPI_INTEGER] = (int64_t)lh_info->MANA_INTEGER;
  upper_to_lower_constants[(int64_t)MPI_INTEGER1] = (int64_t)lh_info->MANA_INTEGER1;
  upper_to_lower_constants[(int64_t)MPI_INTEGER2] = (int64_t)lh_info->MANA_INTEGER2;
  upper_to_lower_constants[(int64_t)MPI_INTEGER4] = (int64_t)lh_info->MANA_INTEGER4;
  upper_to_lower_constants[(int64_t)MPI_INTEGER8] = (int64_t)lh_info->MANA_INTEGER8;
  upper_to_lower_constants[(int64_t)MPI_REAL] = (int64_t)lh_info->MANA_REAL;
  upper_to_lower_constants[(int64_t)MPI_REAL4] = (int64_t)lh_info->MANA_REAL4;
  upper_to_lower_constants[(int64_t)MPI_REAL8] = (int64_t)lh_info->MANA_REAL8;
  upper_to_lower_constants[(int64_t)MPI_REAL16] = (int64_t)lh_info->MANA_REAL16;
  upper_to_lower_constants[(int64_t)MPI_DOUBLE_PRECISION] = (int64_t)lh_info->MANA_DOUBLE_PRECISION;
  upper_to_lower_constants[(int64_t)MPI_COMPLEX] = (int64_t)lh_info->MANA_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_COMPLEX8] = (int64_t)lh_info->MANA_COMPLEX8;
  upper_to_lower_constants[(int64_t)MPI_COMPLEX16] = (int64_t)lh_info->MANA_COMPLEX16;
  upper_to_lower_constants[(int64_t)MPI_COMPLEX32] = (int64_t)lh_info->MANA_COMPLEX32;
  upper_to_lower_constants[(int64_t)MPI_DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_DOUBLE_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_2REAL] = (int64_t)lh_info->MANA_2REAL;
  upper_to_lower_constants[(int64_t)MPI_2REAL] = (int64_t)lh_info->MANA_2REAL;
  upper_to_lower_constants[(int64_t)MPI_2DOUBLE_PRECISION] = (int64_t)lh_info->MANA_2DOUBLE_PRECISION;
  upper_to_lower_constants[(int64_t)MPI_2INTEGER] = (int64_t)lh_info->MANA_2INTEGER;
  upper_to_lower_constants[(int64_t)MPI_INT8_T] = (int64_t)lh_info->MANA_INT8_T;
  upper_to_lower_constants[(int64_t)MPI_UINT8_T] = (int64_t)lh_info->MANA_UINT8_T;
  upper_to_lower_constants[(int64_t)MPI_INT16_T] = (int64_t)lh_info->MANA_INT16_T;
  upper_to_lower_constants[(int64_t)MPI_UINT16_T] = (int64_t)lh_info->MANA_UINT16_T;
  upper_to_lower_constants[(int64_t)MPI_INT32_T] = (int64_t)lh_info->MANA_INT32_T;
  upper_to_lower_constants[(int64_t)MPI_UINT32_T] = (int64_t)lh_info->MANA_UINT32_T;
  upper_to_lower_constants[(int64_t)MPI_INT64_T] = (int64_t)lh_info->MANA_INT64_T;
  upper_to_lower_constants[(int64_t)MPI_UINT64_T] = (int64_t)lh_info->MANA_UINT64_T;
  upper_to_lower_constants[(int64_t)MPI_AINT] = (int64_t)lh_info->MANA_AINT;
  upper_to_lower_constants[(int64_t)MPI_OFFSET] = (int64_t)lh_info->MANA_OFFSET;
  upper_to_lower_constants[(int64_t)MPI_C_BOOL] = (int64_t)lh_info->MANA_C_BOOL;
  upper_to_lower_constants[(int64_t)MPI_C_COMPLEX] = (int64_t)lh_info->MANA_C_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_C_FLOAT_COMPLEX] = (int64_t)lh_info->MANA_C_FLOAT_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_C_DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_C_DOUBLE_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_C_LONG_DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_C_LONG_DOUBLE_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_CXX_BOOL] = (int64_t)lh_info->MANA_CXX_BOOL;
  upper_to_lower_constants[(int64_t)MPI_CXX_FLOAT_COMPLEX] = (int64_t)lh_info->MANA_CXX_FLOAT_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_CXX_DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_CXX_DOUBLE_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_CXX_LONG_DOUBLE_COMPLEX] = (int64_t)lh_info->MANA_CXX_LONG_DOUBLE_COMPLEX;
  upper_to_lower_constants[(int64_t)MPI_COUNT] = (int64_t)lh_info->MANA_COUNT;
  upper_to_lower_constants[(int64_t)MPI_ERRORS_ARE_FATAL] = (int64_t)lh_info->MANA_ERRORS_ARE_FATAL;
  upper_to_lower_constants[(int64_t)MPI_ERRORS_RETURN] = (int64_t)lh_info->MANA_ERRORS_RETURN;
  upper_to_lower_constants[(int64_t)MPI_GROUP_NULL] = (int64_t)lh_info->MANA_GROUP_NULL;
  // Fill in the lower-to-upper mapping
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMM_NULL] = (int64_t)MPI_COMM_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMM_WORLD] = (int64_t)MPI_COMM_WORLD;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMM_SELF] = (int64_t)MPI_COMM_SELF;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REQUEST_NULL] = (int64_t)MPI_REQUEST_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MESSAGE_NULL] = (int64_t)MPI_MESSAGE_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_OP_NULL] = (int64_t)MPI_OP_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_ERRHANDLER_NULL] = (int64_t)MPI_ERRHANDLER_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INFO_NULL] = (int64_t)MPI_INFO_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_WIN_NULL] = (int64_t)MPI_WIN_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_FILE_NULL] = (int64_t)MPI_FILE_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INFO_ENV] = (int64_t)MPI_INFO_ENV;
  lower_to_upper_constants[(int64_t)lh_info->MANA_GROUP_EMPTY] = (int64_t)MPI_GROUP_EMPTY;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MESSAGE_NO_PROC] = (int64_t)MPI_MESSAGE_NO_PROC;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MAX] = (int64_t)MPI_MAX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MIN] = (int64_t)MPI_MIN;
  lower_to_upper_constants[(int64_t)lh_info->MANA_SUM] = (int64_t)MPI_SUM;
  lower_to_upper_constants[(int64_t)lh_info->MANA_PROD] = (int64_t)MPI_PROD;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LAND] = (int64_t)MPI_LAND;
  lower_to_upper_constants[(int64_t)lh_info->MANA_BAND] = (int64_t)MPI_BAND;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOR] = (int64_t)MPI_LOR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_BOR] = (int64_t)MPI_BOR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LXOR] = (int64_t)MPI_LXOR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_BXOR] = (int64_t)MPI_BXOR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MAXLOC] = (int64_t)MPI_MAXLOC;
  lower_to_upper_constants[(int64_t)lh_info->MANA_MINLOC] = (int64_t)MPI_MINLOC;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REPLACE] = (int64_t)MPI_REPLACE;
  lower_to_upper_constants[(int64_t)lh_info->MANA_NO_OP] = (int64_t)MPI_NO_OP;
  lower_to_upper_constants[(int64_t)lh_info->MANA_DATATYPE_NULL] = (int64_t)MPI_DATATYPE_NULL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_BYTE] = (int64_t)MPI_BYTE;
  lower_to_upper_constants[(int64_t)lh_info->MANA_PACKED] = (int64_t)MPI_PACKED;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CHAR] = (int64_t)MPI_CHAR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_SHORT] = (int64_t)MPI_SHORT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INT] = (int64_t)MPI_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG] = (int64_t)MPI_LONG;
  lower_to_upper_constants[(int64_t)lh_info->MANA_FLOAT] = (int64_t)MPI_FLOAT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_DOUBLE] = (int64_t)MPI_DOUBLE;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG_DOUBLE] = (int64_t)MPI_LONG_DOUBLE;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UNSIGNED_CHAR] = (int64_t)MPI_UNSIGNED_CHAR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_SIGNED_CHAR] = (int64_t)MPI_SIGNED_CHAR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UNSIGNED_SHORT] = (int64_t)MPI_UNSIGNED_SHORT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UNSIGNED_LONG] = (int64_t)MPI_UNSIGNED_LONG;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UNSIGNED] = (int64_t)MPI_UNSIGNED;
  lower_to_upper_constants[(int64_t)lh_info->MANA_FLOAT_INT] = (int64_t)MPI_FLOAT_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_DOUBLE_INT] = (int64_t)MPI_DOUBLE_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG_DOUBLE_INT] = (int64_t)MPI_LONG_DOUBLE_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG_INT] = (int64_t)MPI_LONG_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_SHORT_INT] = (int64_t)MPI_SHORT_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_2INT] = (int64_t)MPI_2INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_WCHAR] = (int64_t)MPI_WCHAR;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG_LONG_INT] = (int64_t)MPI_LONG_LONG_INT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LONG_LONG] = (int64_t)MPI_LONG_LONG;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UNSIGNED_LONG_LONG] = (int64_t)MPI_UNSIGNED_LONG_LONG;
#if 0
  lower_to_upper_constants[(int64_t)lh_info->MANA_2COMPLEX] = (int64_t)MPI_2COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CXX_COMPLEX] = (int64_t)MPI_CXX_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_2DOUBLE_COMPLEX] = (int64_t)MPI_2DOUBLE_COMPLEX;
#endif
  lower_to_upper_constants[(int64_t)lh_info->MANA_CHARACTER] = (int64_t)MPI_CHARACTER;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOGICAL] = (int64_t)MPI_LOGICAL;
#if 0
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOGICAL1] = (int64_t)MPI_LOGICAL1;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOGICAL2] = (int64_t)MPI_LOGICAL2;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOGICAL4] = (int64_t)MPI_LOGICAL4;
  lower_to_upper_constants[(int64_t)lh_info->MANA_LOGICAL8] = (int64_t)MPI_LOGICAL8;
#endif
  lower_to_upper_constants[(int64_t)lh_info->MANA_INTEGER] = (int64_t)MPI_INTEGER;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INTEGER1] = (int64_t)MPI_INTEGER1;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INTEGER2] = (int64_t)MPI_INTEGER2;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INTEGER4] = (int64_t)MPI_INTEGER4;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INTEGER8] = (int64_t)MPI_INTEGER8;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REAL] = (int64_t)MPI_REAL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REAL4] = (int64_t)MPI_REAL4;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REAL8] = (int64_t)MPI_REAL8;
  lower_to_upper_constants[(int64_t)lh_info->MANA_REAL16] = (int64_t)MPI_REAL16;
  lower_to_upper_constants[(int64_t)lh_info->MANA_DOUBLE_PRECISION] = (int64_t)MPI_DOUBLE_PRECISION;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMPLEX] = (int64_t)MPI_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMPLEX8] = (int64_t)MPI_COMPLEX8;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMPLEX16] = (int64_t)MPI_COMPLEX16;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COMPLEX32] = (int64_t)MPI_COMPLEX32;
  lower_to_upper_constants[(int64_t)lh_info->MANA_DOUBLE_COMPLEX] = (int64_t)MPI_DOUBLE_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_2REAL] = (int64_t)MPI_2REAL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_2DOUBLE_PRECISION] = (int64_t)MPI_2DOUBLE_PRECISION;
  lower_to_upper_constants[(int64_t)lh_info->MANA_2INTEGER] = (int64_t)MPI_2INTEGER;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INT8_T] = (int64_t)MPI_INT8_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UINT8_T] = (int64_t)MPI_UINT8_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INT16_T] = (int64_t)MPI_INT16_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UINT16_T] = (int64_t)MPI_UINT16_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INT32_T] = (int64_t)MPI_INT32_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UINT32_T] = (int64_t)MPI_UINT32_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_INT64_T] = (int64_t)MPI_INT64_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_UINT64_T] = (int64_t)MPI_UINT64_T;
  lower_to_upper_constants[(int64_t)lh_info->MANA_AINT] = (int64_t)MPI_AINT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_OFFSET] = (int64_t)MPI_OFFSET;
  lower_to_upper_constants[(int64_t)lh_info->MANA_C_BOOL] = (int64_t)MPI_C_BOOL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_C_COMPLEX] = (int64_t)MPI_C_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_C_FLOAT_COMPLEX] = (int64_t)MPI_C_FLOAT_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_C_DOUBLE_COMPLEX] = (int64_t)MPI_C_DOUBLE_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_C_LONG_DOUBLE_COMPLEX] = (int64_t)MPI_C_LONG_DOUBLE_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CXX_BOOL] = (int64_t)MPI_CXX_BOOL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CXX_FLOAT_COMPLEX] = (int64_t)MPI_CXX_FLOAT_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CXX_DOUBLE_COMPLEX] = (int64_t)MPI_CXX_DOUBLE_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_CXX_LONG_DOUBLE_COMPLEX] = (int64_t)MPI_CXX_LONG_DOUBLE_COMPLEX;
  lower_to_upper_constants[(int64_t)lh_info->MANA_COUNT] = (int64_t)MPI_COUNT;
  lower_to_upper_constants[(int64_t)lh_info->MANA_ERRORS_ARE_FATAL] = (int64_t)MPI_ERRORS_ARE_FATAL;
  lower_to_upper_constants[(int64_t)lh_info->MANA_ERRORS_RETURN] = (int64_t)MPI_ERRORS_RETURN;

  // Initialize g_world_comm and g_world_group.
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Comm_dup)(lh_info->MANA_COMM_WORLD, &g_world_comm);
  NEXT_FUNC(Comm_group)(lh_info->MANA_COMM_WORLD, &g_world_group);
  RETURN_TO_UPPER_HALF();
  g_world_comm = new_virt_comm(g_world_comm);

  unsigned int ggid = ggid_table[g_world_comm];
  ggid_table[MPI_COMM_WORLD] = ggid;
  seq_num[ggid] = 0;
  target[ggid] = 0;
}

// Takes a slot from the free list, or else a slot that was never used.
static int
alloc_slot()
{
  if (virt_id_free_head >= 0) {
    int slot = virt_id_free_head;
    virt_id_free_head = slot_entry(slot)->next_free;
    return slot;
  }
  int slot = virt_id_high_water;
  if (slot > MANA_VIRT_ID_SLOT_MASK) {
    fprintf(stderr, "MANA: too many MPI handles in use (%d)\n", slot);
    abort();
  }
  int chunk = slot >> MANA_VIRT_ID_CHUNK_SHIFT;
  if (virt_id_chunks[chunk] == NULL) {
    virt_id_entry *entries =
      (virt_id_entry*)calloc(MANA_VIRT_ID_CHUNK_SIZE, sizeof(virt_id_entry));
    if (entries == NULL) {
      fprintf(stderr, "MANA: out of memory for the virtual-ID table\n");
      abort();
    }
    // Publish the chunk only after it is initialized; see virtual_id.h.
    __atomic_store_n(&virt_id_chunks[chunk], entries, __ATOMIC_RELEASE);
  }
  virt_id_high_water++;
  return slot;
}

mana_mpi_handle add_virt_id(mana_mpi_handle real_id, void *desc, int kind) {
  mana_mpi_handle new_virt_id;
  new_virt_id._handle64 = 0;
  virt_id_entry *entry;
  int handle;
  while (true) {
    int slot = alloc_slot();
    entry = slot_entry(slot);
    // A new handle must never equal a predefined constant: skip the
    // generations of this slot that would produce one.
    int tries;
    for (tries = 0; tries <= MANA_VIRT_ID_GEN_MASK; tries++) {
      handle = kind << MANA_VIRT_ID_KIND_SHIFT |
               (entry->gen & MANA_VIRT_ID_GEN_MASK) << MANA_VIRT_ID_GEN_SHIFT |
               slot;
      if (!is_predefined_id({._handle64 = handle})) {
        break;
      }
      entry->gen++;
    }
    if (tries <= MANA_VIRT_ID_GEN_MASK) {
      break;
    }
    // Every generation of this slot collides; leave the slot unused.
  }
  entry->real_id = real_id;
  entry->desc = desc;
  entry->seq = ++virt_id_next_seq;
  // Publish the handle last; see virtual_id.h.
  __atomic_store_n(&entry->virt, handle, __ATOMIC_RELEASE);
  virt_id_live++;
  new_virt_id._handle = handle;
  return new_virt_id;
}

#define DEBUG_VIRTID
virt_id_entry* get_virt_id_entry(mana_mpi_handle virt_id) {
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  // Should we abort or return an error code?
  if (entry == NULL) {
    // Not a virtual handle in use: a predefined handle (MPI constant),
    // a freed or stale handle, or garbage.
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);

#ifdef DEBUG_VIRTID
    volatile int dummy = 1;
    while (dummy);
#else
    abort();
#endif
  }
  return entry;
}

// The translation of anything that is not a virtual handle in use:
// get_real_id() calls this for predefined constants and invalid handles.
mana_mpi_handle get_real_id_slow(mana_mpi_handle virt_id) {
  std::map<int64_t, int64_t>::iterator it;
  
  /*
   * MPICH represents struct-based MPI datatypes (e.g., MPI_LONG_INT, MPI_DOUBLE_INT) 
   * using negative values in MPI_Datatype. When cast to int64_t, these negative 
   * values will have their upper bits filled with 'f' due to sign extension.
   * 
   * However, during argument passing, the upper bits can be lost, resulting in 
   * zero-filled upper bits. This causes the datatype to appear as a positive number 
   * when referenced as a 64-bit type instead of retaining its original negative value.
   *
   * To handle this discrepancy, we check if the 32-bit handle (`virt_id._handle`) 
   * is less than its 64-bit counterpart (`virt_id._handle64`). If so, we use the 
   * int64_t casted handle for lookup in `upper_to_lower_constants`; otherwise, we 
   * use `_handle64` directly. This ensures consistent mapping and prevents lookup 
   * failures due to sign mismatches.
   *
   * FIXME: This will fail with big-endian CPUs.
   */
  if (virt_id._handle < virt_id._handle64) {
    it = upper_to_lower_constants.find((int64_t)virt_id._handle);
  } else {
    it = upper_to_lower_constants.find(virt_id._handle64);
  }
  
  if (it != upper_to_lower_constants.end()) {
    return {._handle64 = it->second};
  } else {
    // Reports the invalid handle.
    return get_virt_id_entry(virt_id)->real_id;
  }
}

void* get_virt_id_desc(mana_mpi_handle virt_id) {
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  if (entry != NULL) {
    return entry->desc;
  }
  std::map<int64_t, int64_t>::iterator it;
  it = upper_to_lower_constants.find(virt_id._handle64);
  if (it != upper_to_lower_constants.end()) {
    return NULL; // Predefined MPI constants
  } else {
    return get_virt_id_entry(virt_id)->desc;
  }
}

void free_desc(void *desc, int kind) {
  if (kind == MANA_COMM_KIND) {
    mana_comm_desc *comm_desc = (mana_comm_desc*)desc;
    free(comm_desc->global_ranks);
  } else if (kind == MANA_GROUP_KIND) {
    mana_group_desc *group_desc = (mana_group_desc*)desc;
    free(group_desc->global_ranks);
  }
  free(desc);
}

void free_virt_id(mana_mpi_handle virt_id) {
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  // Should we abort or return an error code?
  if (entry == NULL) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  int kind = virt_id._handle >> MANA_VIRT_ID_KIND_SHIFT;
  free_desc(entry->desc, kind);  // free descriptor
  entry->desc = NULL;
  // The next handle that uses this slot gets a new generation.
  entry->gen++;
  __atomic_store_n(&entry->virt, 0, __ATOMIC_RELEASE);
  entry->next_free = virt_id_free_head;
  virt_id_free_head = virt_id._handle & MANA_VIRT_ID_SLOT_MASK;
  virt_id_live--;
}

void update_virt_id(mana_mpi_handle virt_id, mana_mpi_handle real_id) {
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  // Should we abort or return an error code?
  if (entry == NULL) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  __atomic_store_n(&entry->real_id._handle64, real_id._handle64,
                   __ATOMIC_RELAXED);
}

size_t virt_id_live_count() {
  return virt_id_live;
}
