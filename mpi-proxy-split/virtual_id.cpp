#include <pthread.h>
#include <string.h>
#include <mpi.h>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "virtual_id.h"
#include "switch-context.h"
#include "mpi_nextfunc.h"
#include "seq_num.h"
#include "p2p_log_replay.h"
#include <iostream>

MPI_Group g_world_group;
std::map<int64_t, int64_t> lower_to_upper_constants;

// Maps the upper half's predefined constants (MPI_COMM_WORLD, MPI_INT, ...)
// to their lower-half values: open addressing with linear probing.  Written
// only by init_predefined_virt_ids(), at MPI_Init and at restart.
#define MANA_CONSTANTS_TABLE_SIZE 512  // A power of 2
typedef struct {
  int64_t upper;
  int64_t lower;
  bool used;
} mana_constant;
static mana_constant upper_to_lower_constants[MANA_CONSTANTS_TABLE_SIZE];
static int num_constants = 0;

static inline unsigned int
constant_hash(int64_t upper)
{
  return (unsigned int)(((uint64_t)upper * 0x9e3779b97f4a7c15ULL) >> 55) &
         (MANA_CONSTANTS_TABLE_SIZE - 1);
}

// Returns the entry of a predefined constant, or NULL.
static inline mana_constant*
find_constant(int64_t upper)
{
  for (unsigned int i = constant_hash(upper);;
       i = (i + 1) & (MANA_CONSTANTS_TABLE_SIZE - 1)) {
    mana_constant *c = &upper_to_lower_constants[i];
    if (!c->used) {
      return NULL;
    }
    if (c->upper == upper) {
      return c;
    }
  }
}

// Slots that no virtual handle may use, because a predefined constant equals
// one of the slot's handles (for some kind and generation).  Computed from
// the constants' actual values, so it works for any MPI implementation.
static std::vector<bool> reserved_slots;

static inline bool
slot_is_reserved(int slot)
{
  return (size_t)slot < reserved_slots.size() && reserved_slots[slot];
}

// Reserves the slot whose handles could equal the constant 'upper'.
static void
reserve_constant_slot(int64_t upper)
{
  uint64_t value = (uint64_t)upper;
  if (sizeof(MPI_Comm) <= sizeof(int)) {
    value = (uint32_t)value;  // A handle has only 32 bits
  }
  unsigned int kind = value >> MANA_VIRT_ID_KIND_SHIFT;
  if ((value >> 32) != 0 || kind - 1 >= MANA_NUM_KINDS) {
    return;  // No virtual handle has this value
  }
  size_t slot = value & MANA_VIRT_ID_SLOT_MASK;
  if (slot >= reserved_slots.size()) {
    reserved_slots.resize(slot + 1);
  }
  reserved_slots[slot] = true;
}

static void
set_upper_to_lower(int64_t upper, int64_t lower)
{
  reserve_constant_slot(upper);
  for (unsigned int i = constant_hash(upper);;
       i = (i + 1) & (MANA_CONSTANTS_TABLE_SIZE - 1)) {
    mana_constant *c = &upper_to_lower_constants[i];
    if (!c->used || c->upper == upper) {
      if (!c->used) {
        num_constants++;
        // Keep the table sparse so that lookups probe about once.
        assert(num_constants <= MANA_CONSTANTS_TABLE_SIZE / 4);
      }
      c->upper = upper;
      c->lower = lower;
      c->used = true;
      return;
    }
  }
}

// The virtual-ID table; see virtual_id.h for its layout and synchronization.
virt_id_entry *virt_id_chunks[MANA_VIRT_ID_NUM_CHUNKS];
static int virt_id_free_head = -1;   // First slot of the free list, or -1
// Slots [0, high_water) have been used.  Stored with release after the
// chunks are published, so a reader that loads it with acquire sees them.
static int virt_id_high_water = 0;
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

// FNV-1a hash of the global ranks, in order.
static uint64_t
hash_ranks(const int *ranks, int size)
{
  uint64_t h = 14695981039346656037ULL;
  for (int i = 0; i < size; i++) {
    unsigned int r = ranks[i];
    for (int b = 0; b < 4; b++) {
      h = (h ^ ((r >> (8 * b)) & 0xff)) * 1099511628211ULL;
    }
  }
  return h;
}

// The number of communicators created so far for each hash of global ranks.
// All members create a communicator in the same call, so they agree on it.
static std::unordered_map<uint64_t, unsigned int> comm_instances;

int is_predefined_id(mana_mpi_handle id) {
  return find_constant(id._handle64) != NULL;
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
  // The maps never erase entries, so these pointers stay valid.
  desc->ggid = ggid;
  desc->seq_num = &seq_num[ggid];
  desc->target = &target[ggid];
  desc->ranks_hash = hash_ranks(desc->global_ranks, desc->size);
  desc->instance = comm_instances[desc->ranks_hash]++;
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

// Setting 'collective' after the entry is published is safe: the checkpoint
// thread looks for these requests only after drain_mpi_collective().
MPI_Request new_virt_collective_request(MPI_Request real_request) {
  MPI_Request request = new_virt_request(real_request);
  get_virt_id_entry((mana_mpi_handle){.request = request})->collective = true;
  return request;
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

// Free datatype descriptors.  Not locked: only application threads use it,
// one at a time (MANA does not support MPI_THREAD_MULTIPLE).
static mana_datatype_desc *datatype_desc_pool = NULL;

mana_datatype_desc* alloc_datatype_desc() {
  mana_datatype_desc *desc = datatype_desc_pool;
  if (desc != NULL) {
    datatype_desc_pool = desc->next_free;
  } else {
    desc = (mana_datatype_desc*)malloc(sizeof(mana_datatype_desc));
  }
  memset(desc, 0, sizeof(*desc));
  return desc;
}

template<typename F>
static void
for_each_oldtype(const mana_datatype_desc *desc, F f) {
  if (desc->constructor == MANA_TYPE_STRUCT) {
    for (int i = 0; i < desc->count; i++) {
      f(desc->oldtypes[i]);
    }
  } else {
    f(desc->oldtype);
  }
}

static mana_datatype_desc*
datatype_desc(MPI_Datatype type) {
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.datatype = type});
  return entry != NULL ? (mana_datatype_desc*)entry->desc : NULL;
}

MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype,
                               mana_datatype_desc *desc) {
  desc->refs = 1;
  for_each_oldtype(desc, [](MPI_Datatype oldtype) {
    mana_datatype_desc *old = datatype_desc(oldtype);
    if (old != NULL) {  // Not a predefined datatype
      old->refs++;
    }
  });
  mana_mpi_handle virt_id;
  virt_id = add_virt_id((mana_mpi_handle){.datatype = real_datatype}, desc, MANA_DATATYPE_KIND);
  return virt_id.datatype;
}

void commit_virt_datatype(MPI_Datatype type) {
  mana_datatype_desc *desc = datatype_desc(type);
  if (desc != NULL) {
    desc->committed = true;
  }
}

// Drops a reference to a datatype.  At zero, frees it and drops its
// references to the datatypes it was made from.
static void
release_datatype(MPI_Datatype type) {
  mana_datatype_desc *desc = datatype_desc(type);
  if (desc == NULL || --desc->refs > 0) {
    return;
  }
  MPI_Datatype single = desc->oldtype;
  std::vector<MPI_Datatype> olds;
  if (desc->constructor == MANA_TYPE_STRUCT) {
    olds.assign(desc->oldtypes, desc->oldtypes + desc->count);
  }
  free_virt_id((mana_mpi_handle){.datatype = type});  // Frees 'desc'
  if (olds.empty()) {
    release_datatype(single);
  }
  for (MPI_Datatype oldtype : olds) {
    release_datatype(oldtype);
  }
}

// Datatypes the application freed while a pending MPI_Isend/MPI_Irecv used
// them; the call still needs them at checkpoint and restart.
static std::vector<MPI_Datatype> datatypes_freed_while_pending;

void release_freed_datatypes() {
  for (size_t i = 0; i < datatypes_freed_while_pending.size();) {
    MPI_Datatype freed = datatypes_freed_while_pending[i];
    if (pendingCallUsesDatatype(freed)) {
      i++;
    } else {
      datatypes_freed_while_pending.erase(
        datatypes_freed_while_pending.begin() + i);
      release_datatype(freed);
    }
  }
}

void free_virt_datatype(MPI_Datatype type) {
  if (datatype_desc(type) == NULL) {
    return;
  }
  release_freed_datatypes();
  if (pendingCallUsesDatatype(type)) {
    datatypes_freed_while_pending.push_back(type);
  } else {
    release_datatype(type);
  }
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

// Makes the datatype again in the new lower half.  The datatypes it was
// made from are older, so they have been remade already.
void reconstruct_datatype_desc(virt_id_entry *entry) {
  mana_datatype_desc *desc = (mana_datatype_desc*)entry->desc;
  MPI_Datatype old = MPI_DATATYPE_NULL;
  std::vector<MPI_Datatype> olds;
  if (desc->constructor == MANA_TYPE_STRUCT) {
    for (int i = 0; i < desc->count; i++) {
      mana_mpi_handle virt = {.datatype = desc->oldtypes[i]};
      olds.push_back(get_real_id(virt).datatype);
    }
  } else {
    old = get_real_id((mana_mpi_handle){.datatype = desc->oldtype}).datatype;
  }
  MPI_Datatype type = MPI_DATATYPE_NULL;
  int rc = MPI_ERR_TYPE;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  switch (desc->constructor) {
    case MANA_TYPE_CONTIGUOUS:
      rc = NEXT_FUNC(Type_contiguous)(desc->count, old, &type);
      break;
    case MANA_TYPE_VECTOR:
      rc = NEXT_FUNC(Type_vector)(desc->count, desc->blocklength,
                                  desc->stride, old, &type);
      break;
    case MANA_TYPE_HVECTOR:
      rc = NEXT_FUNC(Type_create_hvector)(desc->count, desc->blocklength,
                                          desc->hstride, old, &type);
      break;
    case MANA_TYPE_INDEXED:
      rc = NEXT_FUNC(Type_indexed)(desc->count, desc->blocklengths,
                                   desc->displacements, old, &type);
      break;
    case MANA_TYPE_HINDEXED:
      rc = NEXT_FUNC(Type_create_hindexed)(desc->count, desc->blocklengths,
                                           desc->hdisplacements, old, &type);
      break;
    case MANA_TYPE_STRUCT:
      rc = NEXT_FUNC(Type_create_struct)(desc->count, desc->blocklengths,
                                         desc->hdisplacements, olds.data(),
                                         &type);
      break;
    case MANA_TYPE_DUP:
      rc = NEXT_FUNC(Type_dup)(old, &type);
      break;
    case MANA_TYPE_RESIZED:
      rc = NEXT_FUNC(Type_create_resized)(old, desc->lb, desc->extent, &type);
      break;
  }
  if (rc == MPI_SUCCESS && desc->committed) {
    rc = NEXT_FUNC(Type_commit)(&type);
  }
  RETURN_TO_UPPER_HALF();
  if (rc != MPI_SUCCESS) {
    fprintf(stderr, "Cannot make datatype 0x%x again (constructor %d)\n",
            entry->virt, desc->constructor);
    abort();
  }
  entry->real_id = (mana_mpi_handle){.datatype = type};
}

void reconstruct_descriptors() {
  // Reconstruct by kind, then in creation order (not slot order: slots are
  // reused).  MPI_Comm_create_group is collective, so all ranks must
  // reconstruct their communicators in the same order.
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
    // Predefined objects are never in the table.
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
        reconstruct_datatype_desc(entry);
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
  set_upper_to_lower((int64_t)MPI_GROUP_NULL,
                     (int64_t)lh_info->MANA_GROUP_NULL);
  set_upper_to_lower((int64_t)MPI_COMM_NULL, (int64_t)lh_info->MANA_COMM_NULL);
  set_upper_to_lower((int64_t)MPI_COMM_WORLD,
                     (int64_t)lh_info->MANA_COMM_WORLD);
  set_upper_to_lower((int64_t)MPI_COMM_SELF, (int64_t)lh_info->MANA_COMM_SELF);
  set_upper_to_lower((int64_t)MPI_REQUEST_NULL,
                     (int64_t)lh_info->MANA_REQUEST_NULL);
  set_upper_to_lower((int64_t)MPI_MESSAGE_NULL,
                     (int64_t)lh_info->MANA_MESSAGE_NULL);
  set_upper_to_lower((int64_t)MPI_OP_NULL, (int64_t)lh_info->MANA_OP_NULL);
  set_upper_to_lower((int64_t)MPI_ERRHANDLER_NULL,
                     (int64_t)lh_info->MANA_ERRHANDLER_NULL);
  set_upper_to_lower((int64_t)MPI_INFO_NULL, (int64_t)lh_info->MANA_INFO_NULL);
  set_upper_to_lower((int64_t)MPI_WIN_NULL, (int64_t)lh_info->MANA_WIN_NULL);
  set_upper_to_lower((int64_t)MPI_FILE_NULL, (int64_t)lh_info->MANA_FILE_NULL);
  set_upper_to_lower((int64_t)MPI_INFO_ENV, (int64_t)lh_info->MANA_INFO_ENV);
  set_upper_to_lower((int64_t)MPI_GROUP_EMPTY,
                     (int64_t)lh_info->MANA_GROUP_EMPTY);
  set_upper_to_lower((int64_t)MPI_MESSAGE_NO_PROC,
                     (int64_t)lh_info->MANA_MESSAGE_NO_PROC);
  set_upper_to_lower((int64_t)MPI_MAX, (int64_t)lh_info->MANA_MAX);
  set_upper_to_lower((int64_t)MPI_MIN, (int64_t)lh_info->MANA_MIN);
  set_upper_to_lower((int64_t)MPI_SUM, (int64_t)lh_info->MANA_SUM);
  set_upper_to_lower((int64_t)MPI_PROD, (int64_t)lh_info->MANA_PROD);
  set_upper_to_lower((int64_t)MPI_LAND, (int64_t)lh_info->MANA_LAND);
  set_upper_to_lower((int64_t)MPI_BAND, (int64_t)lh_info->MANA_BAND);
  set_upper_to_lower((int64_t)MPI_LOR, (int64_t)lh_info->MANA_LOR);
  set_upper_to_lower((int64_t)MPI_BOR, (int64_t)lh_info->MANA_BOR);
  set_upper_to_lower((int64_t)MPI_LXOR, (int64_t)lh_info->MANA_LXOR);
  set_upper_to_lower((int64_t)MPI_BXOR, (int64_t)lh_info->MANA_BXOR);
  set_upper_to_lower((int64_t)MPI_MAXLOC, (int64_t)lh_info->MANA_MAXLOC);
  set_upper_to_lower((int64_t)MPI_MINLOC, (int64_t)lh_info->MANA_MINLOC);
  set_upper_to_lower((int64_t)MPI_REPLACE, (int64_t)lh_info->MANA_REPLACE);
  set_upper_to_lower((int64_t)MPI_NO_OP, (int64_t)lh_info->MANA_NO_OP);
  set_upper_to_lower((int64_t)MPI_DATATYPE_NULL,
                     (int64_t)lh_info->MANA_DATATYPE_NULL);
  set_upper_to_lower((int64_t)MPI_BYTE, (int64_t)lh_info->MANA_BYTE);
  set_upper_to_lower((int64_t)MPI_PACKED, (int64_t)lh_info->MANA_PACKED);
  set_upper_to_lower((int64_t)MPI_CHAR, (int64_t)lh_info->MANA_CHAR);
  set_upper_to_lower((int64_t)MPI_SHORT, (int64_t)lh_info->MANA_SHORT);
  set_upper_to_lower((int64_t)MPI_INT, (int64_t)lh_info->MANA_INT);
  set_upper_to_lower((int64_t)MPI_LONG, (int64_t)lh_info->MANA_LONG);
  set_upper_to_lower((int64_t)MPI_FLOAT, (int64_t)lh_info->MANA_FLOAT);
  set_upper_to_lower((int64_t)MPI_DOUBLE, (int64_t)lh_info->MANA_DOUBLE);
  set_upper_to_lower((int64_t)MPI_LONG_DOUBLE,
                     (int64_t)lh_info->MANA_LONG_DOUBLE);
  set_upper_to_lower((int64_t)MPI_UNSIGNED_CHAR,
                     (int64_t)lh_info->MANA_UNSIGNED_CHAR);
  set_upper_to_lower((int64_t)MPI_SIGNED_CHAR,
                     (int64_t)lh_info->MANA_SIGNED_CHAR);
  set_upper_to_lower((int64_t)MPI_UNSIGNED_SHORT,
                     (int64_t)lh_info->MANA_UNSIGNED_SHORT);
  set_upper_to_lower((int64_t)MPI_UNSIGNED_LONG,
                     (int64_t)lh_info->MANA_UNSIGNED_LONG);
  set_upper_to_lower((int64_t)MPI_UNSIGNED, (int64_t)lh_info->MANA_UNSIGNED);
  set_upper_to_lower((int64_t)MPI_FLOAT_INT, (int64_t)lh_info->MANA_FLOAT_INT);
  set_upper_to_lower((int64_t)MPI_DOUBLE_INT,
                     (int64_t)lh_info->MANA_DOUBLE_INT);
  set_upper_to_lower((int64_t)MPI_LONG_DOUBLE_INT,
                     (int64_t)lh_info->MANA_LONG_DOUBLE_INT);
  set_upper_to_lower((int64_t)MPI_LONG_INT, (int64_t)lh_info->MANA_LONG_INT);
  set_upper_to_lower((int64_t)MPI_SHORT_INT, (int64_t)lh_info->MANA_SHORT_INT);
  set_upper_to_lower((int64_t)MPI_2INT, (int64_t)lh_info->MANA_2INT);
  set_upper_to_lower((int64_t)MPI_WCHAR, (int64_t)lh_info->MANA_WCHAR);
  set_upper_to_lower((int64_t)MPI_LONG_LONG_INT,
                     (int64_t)lh_info->MANA_LONG_LONG_INT);
  set_upper_to_lower((int64_t)MPI_LONG_LONG, (int64_t)lh_info->MANA_LONG_LONG);
  set_upper_to_lower((int64_t)MPI_UNSIGNED_LONG_LONG,
                     (int64_t)lh_info->MANA_UNSIGNED_LONG_LONG);
#if 0
  set_upper_to_lower((int64_t)MPI_2COMPLEX, (int64_t)lh_info->MANA_2COMPLEX);
  set_upper_to_lower((int64_t)MPI_CXX_COMPLEX,
                     (int64_t)lh_info->MANA_CXX_COMPLEX);
  set_upper_to_lower((int64_t)MPI_2DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_2DOUBLE_COMPLEX);
#endif
  set_upper_to_lower((int64_t)MPI_CHARACTER, (int64_t)lh_info->MANA_CHARACTER);
  set_upper_to_lower((int64_t)MPI_LOGICAL, (int64_t)lh_info->MANA_LOGICAL);
#if 0
  set_upper_to_lower((int64_t)MPI_LOGICAL1, (int64_t)lh_info->MANA_LOGICAL1);
  set_upper_to_lower((int64_t)MPI_LOGICAL2, (int64_t)lh_info->MANA_LOGICAL2);
  set_upper_to_lower((int64_t)MPI_LOGICAL4, (int64_t)lh_info->MANA_LOGICAL4);
  set_upper_to_lower((int64_t)MPI_LOGICAL8, (int64_t)lh_info->MANA_LOGICAL8);
#endif
  set_upper_to_lower((int64_t)MPI_INTEGER, (int64_t)lh_info->MANA_INTEGER);
  set_upper_to_lower((int64_t)MPI_INTEGER1, (int64_t)lh_info->MANA_INTEGER1);
  set_upper_to_lower((int64_t)MPI_INTEGER2, (int64_t)lh_info->MANA_INTEGER2);
  set_upper_to_lower((int64_t)MPI_INTEGER4, (int64_t)lh_info->MANA_INTEGER4);
  set_upper_to_lower((int64_t)MPI_INTEGER8, (int64_t)lh_info->MANA_INTEGER8);
  set_upper_to_lower((int64_t)MPI_REAL, (int64_t)lh_info->MANA_REAL);
  set_upper_to_lower((int64_t)MPI_REAL4, (int64_t)lh_info->MANA_REAL4);
  set_upper_to_lower((int64_t)MPI_REAL8, (int64_t)lh_info->MANA_REAL8);
  set_upper_to_lower((int64_t)MPI_REAL16, (int64_t)lh_info->MANA_REAL16);
  set_upper_to_lower((int64_t)MPI_DOUBLE_PRECISION,
                     (int64_t)lh_info->MANA_DOUBLE_PRECISION);
  set_upper_to_lower((int64_t)MPI_COMPLEX, (int64_t)lh_info->MANA_COMPLEX);
  set_upper_to_lower((int64_t)MPI_COMPLEX8, (int64_t)lh_info->MANA_COMPLEX8);
  set_upper_to_lower((int64_t)MPI_COMPLEX16, (int64_t)lh_info->MANA_COMPLEX16);
  set_upper_to_lower((int64_t)MPI_COMPLEX32, (int64_t)lh_info->MANA_COMPLEX32);
  set_upper_to_lower((int64_t)MPI_DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_DOUBLE_COMPLEX);
  set_upper_to_lower((int64_t)MPI_2REAL, (int64_t)lh_info->MANA_2REAL);
  set_upper_to_lower((int64_t)MPI_2REAL, (int64_t)lh_info->MANA_2REAL);
  set_upper_to_lower((int64_t)MPI_2DOUBLE_PRECISION,
                     (int64_t)lh_info->MANA_2DOUBLE_PRECISION);
  set_upper_to_lower((int64_t)MPI_2INTEGER, (int64_t)lh_info->MANA_2INTEGER);
  set_upper_to_lower((int64_t)MPI_INT8_T, (int64_t)lh_info->MANA_INT8_T);
  set_upper_to_lower((int64_t)MPI_UINT8_T, (int64_t)lh_info->MANA_UINT8_T);
  set_upper_to_lower((int64_t)MPI_INT16_T, (int64_t)lh_info->MANA_INT16_T);
  set_upper_to_lower((int64_t)MPI_UINT16_T, (int64_t)lh_info->MANA_UINT16_T);
  set_upper_to_lower((int64_t)MPI_INT32_T, (int64_t)lh_info->MANA_INT32_T);
  set_upper_to_lower((int64_t)MPI_UINT32_T, (int64_t)lh_info->MANA_UINT32_T);
  set_upper_to_lower((int64_t)MPI_INT64_T, (int64_t)lh_info->MANA_INT64_T);
  set_upper_to_lower((int64_t)MPI_UINT64_T, (int64_t)lh_info->MANA_UINT64_T);
  set_upper_to_lower((int64_t)MPI_AINT, (int64_t)lh_info->MANA_AINT);
  set_upper_to_lower((int64_t)MPI_OFFSET, (int64_t)lh_info->MANA_OFFSET);
  set_upper_to_lower((int64_t)MPI_C_BOOL, (int64_t)lh_info->MANA_C_BOOL);
  set_upper_to_lower((int64_t)MPI_C_COMPLEX, (int64_t)lh_info->MANA_C_COMPLEX);
  set_upper_to_lower((int64_t)MPI_C_FLOAT_COMPLEX,
                     (int64_t)lh_info->MANA_C_FLOAT_COMPLEX);
  set_upper_to_lower((int64_t)MPI_C_DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_C_DOUBLE_COMPLEX);
  set_upper_to_lower((int64_t)MPI_C_LONG_DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_C_LONG_DOUBLE_COMPLEX);
  set_upper_to_lower((int64_t)MPI_CXX_BOOL, (int64_t)lh_info->MANA_CXX_BOOL);
  set_upper_to_lower((int64_t)MPI_CXX_FLOAT_COMPLEX,
                     (int64_t)lh_info->MANA_CXX_FLOAT_COMPLEX);
  set_upper_to_lower((int64_t)MPI_CXX_DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_CXX_DOUBLE_COMPLEX);
  set_upper_to_lower((int64_t)MPI_CXX_LONG_DOUBLE_COMPLEX,
                     (int64_t)lh_info->MANA_CXX_LONG_DOUBLE_COMPLEX);
  set_upper_to_lower((int64_t)MPI_COUNT, (int64_t)lh_info->MANA_COUNT);
  set_upper_to_lower((int64_t)MPI_ERRORS_ARE_FATAL,
                     (int64_t)lh_info->MANA_ERRORS_ARE_FATAL);
  set_upper_to_lower((int64_t)MPI_ERRORS_RETURN,
                     (int64_t)lh_info->MANA_ERRORS_RETURN);
  set_upper_to_lower((int64_t)MPI_GROUP_NULL,
                     (int64_t)lh_info->MANA_GROUP_NULL);
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

// Takes a slot from the free list, or else the next unused, unreserved slot.
static int
alloc_slot()
{
  if (virt_id_free_head >= 0) {
    int slot = virt_id_free_head;
    virt_id_free_head = slot_entry(slot)->next_free;
    return slot;
  }
  int slot = virt_id_high_water;
  while (slot_is_reserved(slot)) {
    slot++;
  }
  if (slot > MANA_VIRT_ID_SLOT_MASK) {
    fprintf(stderr, "MANA: too many MPI handles in use (%d)\n", slot);
    abort();
  }
  // The table has an entry for every slot below the high-water mark, even a
  // reserved one (it stays free).
  for (int chunk = virt_id_high_water >> MANA_VIRT_ID_CHUNK_SHIFT;
       chunk <= slot >> MANA_VIRT_ID_CHUNK_SHIFT; chunk++) {
    if (virt_id_chunks[chunk] != NULL) {
      continue;
    }
    virt_id_entry *entries =
      (virt_id_entry*)calloc(MANA_VIRT_ID_CHUNK_SIZE, sizeof(virt_id_entry));
    if (entries == NULL) {
      fprintf(stderr, "MANA: out of memory for the virtual-ID table\n");
      abort();
    }
    // Publish the chunk only after it is initialized; see virtual_id.h.
    __atomic_store_n(&virt_id_chunks[chunk], entries, __ATOMIC_RELEASE);
  }
  __atomic_store_n(&virt_id_high_water, slot + 1, __ATOMIC_RELEASE);
  return slot;
}

mana_mpi_handle add_virt_id(mana_mpi_handle real_id, void *desc, int kind) {
  mana_mpi_handle new_virt_id;
  new_virt_id._handle64 = 0;
  int slot = alloc_slot();
  virt_id_entry *entry = slot_entry(slot);
  int handle = kind << MANA_VIRT_ID_KIND_SHIFT |
               (entry->gen & MANA_VIRT_ID_GEN_MASK) << MANA_VIRT_ID_GEN_SHIFT |
               slot;
  entry->real_id = real_id;
  entry->desc = desc;
  entry->seq = ++virt_id_next_seq;
  entry->call.type = UNKNOW_REQUEST;
  entry->collective = false;
  entry->completed = false;
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
    // A predefined constant, a freed or stale handle, or garbage.
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

// get_real_id() for anything but a virtual handle in use: a predefined
// constant or an invalid handle.
mana_mpi_handle get_real_id_slow(mana_mpi_handle virt_id) {
  mana_constant *c;
  
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
    c = find_constant((int64_t)virt_id._handle);
  } else {
    c = find_constant(virt_id._handle64);
  }
  
  if (c != NULL) {
    return {._handle64 = c->lower};
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
  if (find_constant(virt_id._handle64) != NULL) {
    return NULL; // Predefined MPI constants
  } else {
    return get_virt_id_entry(virt_id)->desc;
  }
}

void free_desc(void *desc, int kind) {
  if (kind == MANA_DATATYPE_KIND) {
    mana_datatype_desc *type_desc = (mana_datatype_desc*)desc;
    free(type_desc->blocklengths);
    free(type_desc->displacements);
    free(type_desc->hdisplacements);
    free(type_desc->oldtypes);
    type_desc->next_free = datatype_desc_pool;
    datatype_desc_pool = type_desc;
    return;
  }
  if (kind == MANA_COMM_KIND) {
    mana_comm_desc *comm_desc = (mana_comm_desc*)desc;
    free(comm_desc->global_ranks);
  } else if (kind == MANA_GROUP_KIND) {
    mana_group_desc *group_desc = (mana_group_desc*)desc;
    free(group_desc->global_ranks);
  }
  free(desc);
}

// Held while a communicator's descriptor is freed, and while the checkpoint
// thread reads the descriptors (find_virt_comm()).
static pthread_mutex_t comm_desc_lock = PTHREAD_MUTEX_INITIALIZER;

void free_virt_id(mana_mpi_handle virt_id) {
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  // Should we abort or return an error code?
  if (entry == NULL) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  // A pending call must be removed first (clearPendingRequestFromLog()).
  assert(entry->call.type == UNKNOW_REQUEST);
  int kind = virt_id._handle >> MANA_VIRT_ID_KIND_SHIFT;
  void *desc = entry->desc;
  entry->gen++;
  // The checkpoint thread may be reading a communicator's descriptor
  // (find_virt_comm()).
  if (kind == MANA_COMM_KIND) {
    pthread_mutex_lock(&comm_desc_lock);
  }
  __atomic_store_n(&entry->virt, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&entry->desc, (void*)NULL, __ATOMIC_RELEASE);
  free_desc(desc, kind);  // free descriptor
  if (kind == MANA_COMM_KIND) {
    pthread_mutex_unlock(&comm_desc_lock);
  }
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

void complete_virt_request(MPI_Request request, const MPI_Status *status) {
  virt_id_entry *entry =
    get_virt_id_entry((mana_mpi_handle){.request = request});
  entry->status = *status;
  // An application thread may be in MPI_Wait on this request: publish the
  // status before the real request becomes MPI_REQUEST_NULL.
  __atomic_store_n(&entry->completed, true, __ATOMIC_RELEASE);
  mana_mpi_handle real_request;
  real_request._handle64 = 0;
  real_request.request = MPI_REQUEST_NULL;
  __atomic_store_n(&entry->real_id._handle64, real_request._handle64,
                   __ATOMIC_RELEASE);
}

bool completed_request_status(MPI_Request request, MPI_Status *status) {
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = request});
  if (entry == NULL || !__atomic_load_n(&entry->completed, __ATOMIC_ACQUIRE)) {
    return false;
  }
  if (status != NULL) {
    *status = entry->status;
  }
  return true;
}

size_t virt_id_live_count() {
  return virt_id_live;
}

std::vector<MPI_Comm> live_virt_comms() {
  std::vector<std::pair<uint64_t, MPI_Comm> > comms;
  int high_water = __atomic_load_n(&virt_id_high_water, __ATOMIC_ACQUIRE);
  for (int slot = 0; slot < high_water; slot++) {
    virt_id_entry *entry = slot_entry(slot);
    int handle = __atomic_load_n(&entry->virt, __ATOMIC_ACQUIRE);
    if (handle != 0 && handle >> MANA_VIRT_ID_KIND_SHIFT == MANA_COMM_KIND) {
      comms.push_back(std::make_pair(entry->seq, (MPI_Comm)handle));
    }
  }
  std::sort(comms.begin(), comms.end());
  std::vector<MPI_Comm> result;
  for (std::pair<uint64_t, MPI_Comm> comm : comms) {
    result.push_back(comm.second);
  }
  return result;
}

MPI_Comm find_virt_comm(uint64_t ranks_hash, unsigned int instance) {
  MPI_Comm found = MPI_COMM_NULL;
  pthread_mutex_lock(&comm_desc_lock);
  int high_water = __atomic_load_n(&virt_id_high_water, __ATOMIC_ACQUIRE);
  for (int slot = 0; slot < high_water && found == MPI_COMM_NULL; slot++) {
    virt_id_entry *entry = slot_entry(slot);
    int handle = __atomic_load_n(&entry->virt, __ATOMIC_ACQUIRE);
    if (handle != 0 && handle >> MANA_VIRT_ID_KIND_SHIFT == MANA_COMM_KIND) {
      mana_comm_desc *desc = (mana_comm_desc*)
        __atomic_load_n(&entry->desc, __ATOMIC_ACQUIRE);
      if (desc != NULL && desc->ranks_hash == ranks_hash &&
          desc->instance == instance) {
        found = (MPI_Comm)handle;
      }
    }
  }
  pthread_mutex_unlock(&comm_desc_lock);
  return found;
}

std::vector<MPI_Request> pending_collective_requests() {
  std::vector<std::pair<uint64_t, MPI_Request> > requests;
  int high_water = __atomic_load_n(&virt_id_high_water, __ATOMIC_ACQUIRE);
  for (int slot = 0; slot < high_water; slot++) {
    virt_id_entry *entry = slot_entry(slot);
    int handle = __atomic_load_n(&entry->virt, __ATOMIC_ACQUIRE);
    if (handle != 0 && handle >> MANA_VIRT_ID_KIND_SHIFT == MANA_REQUEST_KIND &&
        entry->collective &&
        entry->real_id.request != MPI_REQUEST_NULL) {
      requests.push_back(std::make_pair(entry->seq, (MPI_Request)handle));
    }
  }
  std::sort(requests.begin(), requests.end());
  std::vector<MPI_Request> result;
  for (std::pair<uint64_t, MPI_Request> request : requests) {
    result.push_back(request.second);
  }
  return result;
}
