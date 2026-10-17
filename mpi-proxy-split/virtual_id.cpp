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
// The inverse of upper_to_lower_constants below, keyed like it.
static std::map<int64_t, int64_t> lower_to_upper_constants;
// For each kind (MANA_*_KIND), the predefined constants by Fortran handle.
static std::unordered_map<int, int64_t>
  fortran_constants[MANA_NUM_HANDLE_KINDS + 1];

// Maps the upper half's predefined constants (MPI_COMM_WORLD, MPI_INT, ...)
// to their lower-half values: open addressing with linear probing.  Written
// only by init_predefined_virt_ids(), at MPI_Init and at restart.
#define MANA_CONSTANTS_TABLE_SIZE 512  // A power of 2
typedef struct {
  int64_t upper;
  int64_t lower;
  int fortran;  // The Fortran handle (mpif.h's)
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

// The key of a handle in the table of constants.  With 32-bit handles
// (MPICH), only the low 32 bits of the union hold the handle, and the table
// has them sign-extended: MPICH's struct datatypes, e.g. MPI_LONG_INT, are
// negative.  With 64-bit handles (Open MPI), the key is the whole value.
// MPI_File is a pointer even in MPICH, but its only constant, MPI_FILE_NULL,
// is 0.
static inline int64_t
constant_key(mana_mpi_handle id)
{
  return sizeof(MPI_Comm) <= sizeof(int) ? (int64_t)id._handle
                                         : id._handle64;
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

typedef int (*handle_c2f_t)(int kind, int64_t handle);

static void
set_upper_to_lower(int kind, int64_t upper, int64_t lower)
{
  int fortran;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  fortran = ((handle_c2f_t)lh_info->handle_c2f)(kind, lower);
  RETURN_TO_UPPER_HALF();
  fortran_constants[kind][fortran] = upper;
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
      c->fortran = fortran;
      c->used = true;
      lower_to_upper_constants[lower] = upper;
      return;
    }
  }
}

int64_t lower_of_constant(int64_t key) {
  mana_constant *c = find_constant(key);
  return c != NULL ? c->lower : key;
}

int64_t upper_of_constant(int64_t key) {
  std::map<int64_t, int64_t>::iterator it = lower_to_upper_constants.find(key);
  return it != lower_to_upper_constants.end() ? it->second : key;
}

int64_t fortran_to_handle(int kind, int fortran) {
  if (kind <= MANA_NUM_KINDS) {
    mana_mpi_handle handle;
    handle._handle64 = (uint32_t)fortran;
    if (lookup_virt_id_entry(handle) != NULL) {
      return handle._handle64;
    }
  }
  std::unordered_map<int, int64_t>::iterator it =
    fortran_constants[kind].find(fortran);
  if (it != fortran_constants[kind].end()) {
    return it->second;
  }
  return fortran;  // Invalid: the C wrapper reports it.
}

int handle_to_fortran(int kind, int64_t key) {
  mana_mpi_handle handle;
  handle._handle64 = key;
  if (kind <= MANA_NUM_KINDS && lookup_virt_id_entry(handle) != NULL) {
    return handle._handle;
  }
  mana_constant *c = find_constant(key);
  return c != NULL ? c->fortran : (int)key;
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
  return find_constant(constant_key(id)) != NULL;
}

MPI_Comm new_virt_comm(MPI_Comm real_comm) {
  if (real_comm == lh_info->MANA_COMM_NULL) {
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
  NEXT_FUNC(Group_free)(&local_group);
  RETURN_TO_UPPER_HALF();
  free(local_ranks);

  // MPI_Cart_create, MPI_Cart_sub and MPI_Comm_dup of a Cartesian
  // communicator make a Cartesian communicator.
  int topology = MPI_UNDEFINED;
  desc->cart_ndims = -1;
  desc->cart_dims = NULL;
  desc->cart_periods = NULL;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Topo_test)(real_comm, &topology);
  if (topology == MPI_CART) {
    NEXT_FUNC(Cartdim_get)(real_comm, &desc->cart_ndims);
  }
  RETURN_TO_UPPER_HALF();
  if (desc->cart_ndims > 0) {
    int ndims = desc->cart_ndims;
    std::vector<int> coords(ndims);
    desc->cart_dims = (int*)malloc(sizeof(int) * ndims);
    desc->cart_periods = (int*)malloc(sizeof(int) * ndims);
    JUMP_TO_LOWER_HALF(lh_info->fsaddr);
    NEXT_FUNC(Cart_get)(real_comm, ndims, desc->cart_dims, desc->cart_periods,
                        coords.data());
    RETURN_TO_UPPER_HALF();
  }

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
  free(local_ranks);

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
  MPI_Comm comm;
  mana_comm_desc *desc = (mana_comm_desc*)entry->desc;
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);
  NEXT_FUNC(Group_incl)(g_world_group, desc->size, desc->global_ranks, &group);
  NEXT_FUNC(Comm_create_group)(lh_info->MANA_COMM_WORLD, group, 0, &comm);
  NEXT_FUNC(Group_free)(&group);
  if (desc->cart_ndims >= 0) {
    MPI_Comm plain = comm;
    NEXT_FUNC(Cart_create)(plain, desc->cart_ndims, desc->cart_dims,
                           desc->cart_periods, 0, &comm);
    NEXT_FUNC(Comm_free)(&plain);
  }
  RETURN_TO_UPPER_HALF();
  entry->real_id.comm = comm;
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
  // At restart, the lower half's values are new.
  lower_to_upper_constants.clear();
  for (int kind = 0; kind <= MANA_NUM_HANDLE_KINDS; kind++) {
    fortran_constants[kind].clear();
  }
  // Fill in the upper-to-lower mapping, and the Fortran handles.
#define PREDEFINED(kind, name) \
  set_upper_to_lower(kind, (int64_t)MPI_##name, (int64_t)lh_info->MANA_##name)
  PREDEFINED(MANA_GROUP_KIND, GROUP_NULL);
  PREDEFINED(MANA_COMM_KIND, COMM_NULL);
  PREDEFINED(MANA_COMM_KIND, COMM_WORLD);
  PREDEFINED(MANA_COMM_KIND, COMM_SELF);
  PREDEFINED(MANA_REQUEST_KIND, REQUEST_NULL);
  PREDEFINED(MANA_MESSAGE_KIND, MESSAGE_NULL);
  PREDEFINED(MANA_OP_KIND, OP_NULL);
  PREDEFINED(MANA_ERRHANDLER_KIND, ERRHANDLER_NULL);
  PREDEFINED(MANA_INFO_KIND, INFO_NULL);
  PREDEFINED(MANA_WIN_KIND, WIN_NULL);
  PREDEFINED(MANA_FILE_KIND, FILE_NULL);
  PREDEFINED(MANA_INFO_KIND, INFO_ENV);
  PREDEFINED(MANA_GROUP_KIND, GROUP_EMPTY);
  PREDEFINED(MANA_MESSAGE_KIND, MESSAGE_NO_PROC);
  PREDEFINED(MANA_OP_KIND, MAX);
  PREDEFINED(MANA_OP_KIND, MIN);
  PREDEFINED(MANA_OP_KIND, SUM);
  PREDEFINED(MANA_OP_KIND, PROD);
  PREDEFINED(MANA_OP_KIND, LAND);
  PREDEFINED(MANA_OP_KIND, BAND);
  PREDEFINED(MANA_OP_KIND, LOR);
  PREDEFINED(MANA_OP_KIND, BOR);
  PREDEFINED(MANA_OP_KIND, LXOR);
  PREDEFINED(MANA_OP_KIND, BXOR);
  PREDEFINED(MANA_OP_KIND, MAXLOC);
  PREDEFINED(MANA_OP_KIND, MINLOC);
  PREDEFINED(MANA_OP_KIND, REPLACE);
  PREDEFINED(MANA_OP_KIND, NO_OP);
  PREDEFINED(MANA_DATATYPE_KIND, DATATYPE_NULL);
  PREDEFINED(MANA_DATATYPE_KIND, BYTE);
  PREDEFINED(MANA_DATATYPE_KIND, PACKED);
  PREDEFINED(MANA_DATATYPE_KIND, CHAR);
  PREDEFINED(MANA_DATATYPE_KIND, SHORT);
  PREDEFINED(MANA_DATATYPE_KIND, INT);
  PREDEFINED(MANA_DATATYPE_KIND, LONG);
  PREDEFINED(MANA_DATATYPE_KIND, FLOAT);
  PREDEFINED(MANA_DATATYPE_KIND, DOUBLE);
  PREDEFINED(MANA_DATATYPE_KIND, LONG_DOUBLE);
  PREDEFINED(MANA_DATATYPE_KIND, UNSIGNED_CHAR);
  PREDEFINED(MANA_DATATYPE_KIND, SIGNED_CHAR);
  PREDEFINED(MANA_DATATYPE_KIND, UNSIGNED_SHORT);
  PREDEFINED(MANA_DATATYPE_KIND, UNSIGNED_LONG);
  PREDEFINED(MANA_DATATYPE_KIND, UNSIGNED);
  PREDEFINED(MANA_DATATYPE_KIND, FLOAT_INT);
  PREDEFINED(MANA_DATATYPE_KIND, DOUBLE_INT);
  PREDEFINED(MANA_DATATYPE_KIND, LONG_DOUBLE_INT);
  PREDEFINED(MANA_DATATYPE_KIND, LONG_INT);
  PREDEFINED(MANA_DATATYPE_KIND, SHORT_INT);
  PREDEFINED(MANA_DATATYPE_KIND, 2INT);
  PREDEFINED(MANA_DATATYPE_KIND, WCHAR);
  PREDEFINED(MANA_DATATYPE_KIND, LONG_LONG_INT);
  PREDEFINED(MANA_DATATYPE_KIND, LONG_LONG);
  PREDEFINED(MANA_DATATYPE_KIND, UNSIGNED_LONG_LONG);
#if 0
  PREDEFINED(MANA_DATATYPE_KIND, 2COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, CXX_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, 2DOUBLE_COMPLEX);
#endif
  PREDEFINED(MANA_DATATYPE_KIND, CHARACTER);
  PREDEFINED(MANA_DATATYPE_KIND, LOGICAL);
#if 0
  PREDEFINED(MANA_DATATYPE_KIND, LOGICAL1);
  PREDEFINED(MANA_DATATYPE_KIND, LOGICAL2);
  PREDEFINED(MANA_DATATYPE_KIND, LOGICAL4);
  PREDEFINED(MANA_DATATYPE_KIND, LOGICAL8);
#endif
  PREDEFINED(MANA_DATATYPE_KIND, INTEGER);
  PREDEFINED(MANA_DATATYPE_KIND, INTEGER1);
  PREDEFINED(MANA_DATATYPE_KIND, INTEGER2);
  PREDEFINED(MANA_DATATYPE_KIND, INTEGER4);
  PREDEFINED(MANA_DATATYPE_KIND, INTEGER8);
  PREDEFINED(MANA_DATATYPE_KIND, REAL);
  PREDEFINED(MANA_DATATYPE_KIND, REAL4);
  PREDEFINED(MANA_DATATYPE_KIND, REAL8);
  PREDEFINED(MANA_DATATYPE_KIND, REAL16);
  PREDEFINED(MANA_DATATYPE_KIND, DOUBLE_PRECISION);
  PREDEFINED(MANA_DATATYPE_KIND, COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, COMPLEX8);
  PREDEFINED(MANA_DATATYPE_KIND, COMPLEX16);
  PREDEFINED(MANA_DATATYPE_KIND, COMPLEX32);
  PREDEFINED(MANA_DATATYPE_KIND, DOUBLE_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, 2REAL);
  PREDEFINED(MANA_DATATYPE_KIND, 2DOUBLE_PRECISION);
  PREDEFINED(MANA_DATATYPE_KIND, 2INTEGER);
  PREDEFINED(MANA_DATATYPE_KIND, INT8_T);
  PREDEFINED(MANA_DATATYPE_KIND, UINT8_T);
  PREDEFINED(MANA_DATATYPE_KIND, INT16_T);
  PREDEFINED(MANA_DATATYPE_KIND, UINT16_T);
  PREDEFINED(MANA_DATATYPE_KIND, INT32_T);
  PREDEFINED(MANA_DATATYPE_KIND, UINT32_T);
  PREDEFINED(MANA_DATATYPE_KIND, INT64_T);
  PREDEFINED(MANA_DATATYPE_KIND, UINT64_T);
  PREDEFINED(MANA_DATATYPE_KIND, AINT);
  PREDEFINED(MANA_DATATYPE_KIND, OFFSET);
  PREDEFINED(MANA_DATATYPE_KIND, C_BOOL);
  PREDEFINED(MANA_DATATYPE_KIND, C_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, C_FLOAT_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, C_DOUBLE_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, C_LONG_DOUBLE_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, CXX_BOOL);
  PREDEFINED(MANA_DATATYPE_KIND, CXX_FLOAT_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, CXX_DOUBLE_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, CXX_LONG_DOUBLE_COMPLEX);
  PREDEFINED(MANA_DATATYPE_KIND, COUNT);
  PREDEFINED(MANA_ERRHANDLER_KIND, ERRORS_ARE_FATAL);
  PREDEFINED(MANA_ERRHANDLER_KIND, ERRORS_RETURN);
#undef PREDEFINED

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
  mana_constant *c = find_constant(constant_key(virt_id));
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
  if (find_constant(constant_key(virt_id)) != NULL) {
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
    free(comm_desc->cart_dims);
    free(comm_desc->cart_periods);
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
  real_request.request = lh_info->MANA_REQUEST_NULL;
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
        entry->real_id.request != lh_info->MANA_REQUEST_NULL) {
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
