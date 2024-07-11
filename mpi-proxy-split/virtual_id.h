#ifndef MANA_VIRTUAL_ID_H
#define MANA_VIRTUAL_ID_H

#include <mpi.h>
#include <assert.h>
#include <stdlib.h>

#define VIRT_ID_TABLE_SIZE 10000
#define VIRT_ID_MASK_LEN 4
#define VIRT_ID_MASK 0xf

#ifdef MPICH
typedef int mana_handle;
#elif OPEN_MPI
typedef void* mana_handle;
#elif EXAMPI
typedef void* mana_handle;
#endif

typedef mana_handle MPI_Comm;
typedef mana_handle MPI_Group;
typedef mana_handle MPI_Datatype;
typedef mana_handle MPI_Op;
typedef mana_handle MPI_Request;

typedef struct {
  MPI_Group group; // virtual id of the group
  group_desc *group_desc;
  int ggid;
  unsigned long seq_num;
  unsigned long target_num;
} mana_comm_desc;

typedef struct {
  int size;
  int rank;
  int *global_ranks;
} mana_group_desc;

typedef struct {
  // For now, there's no need for additional informations.
  // NULL will be used in virt_id_entry of request.
} mana_request_desc;

typedef struct {
  MPI_User_function *user_fn;
  int commute;
} mana_op_desc;

typedef struct {
  int num_intergers;
  int *integers;
  int num_addresses;
  MPI_Aint *addresses;
  int num_large_counts;
  int *large_counts;
  int num_datatypes;
  MPI_Datatype *datatypes; // hmmm.. hierarchical restore?
  int combiner;
  // if is_freed is true, then we should not update the descriptor on a checkpoint.
  bool is_freed;
} mana_datatype_desc;

typedef enum {
  COMM_MASK;
  GROUP_MASK;
  DATATYPE_MASK;
  OP_MASK;
  REQUEST_MASK;
} virt_id_mask;

typedef struct {
  mana_handle real_id;
  void *desc;
  int used;
} virt_id_entry;

typedef struct {
  virt_id_entry *entries;
  int capacity;
  int size;
  unsigned int curr_index;
} virt_id_table;

inline void init_virt_id_virt_ids(virt_id_virt_ids *virt_ids) {
  virt_ids->entries = calloc(VIRT_ID_TABLE_SIZE, sizeof(virt_id_entry));
  virt_ids->capacity = VIRT_ID_TABLE_SIZE;
  virt_ids->size = 0;
  virt_ids->curr_index = 0;
}

inline void free_virt_id_virt_ids() {
  free(virt_ids->entries);
}

inline mana_handle add_virt_id(mana_handle rea_id,
                               void *desc, virt_id_mask mask) {
  assert(virt_ids->size < virt_ids->capacity);
  // TODO: If the virt_ids is half full, extend the buffer with realloc.
  while (virt_ids->entries[virt_ids->curr_index].used != 0) {
    virt_ids->curr_index = (virt_ids->curr_index + 1) % virt_ids->capacity;
  }
  int index = virt_ids->curr_index;
  virt_ids->entries[index].real_id = real_id;
  virt_ids->entries[index].desc = desc;
  virt_ids->entries[index].used = 1;
  virt_ids->size++;
  unsigned int virt_id = virt_ids->curr_index;
  virt_id = virt_id << VIRT_ID_MASK_LEN;
  virt_id = virt_id | mask;
  return (mana_handle)virt_id;
}

inline virt_id_entry* get_virt_id_entry(mana_handle virt_id) {
  unsigned int index = virt_id >> VIRT_ID_MASK_LEN;
  assert(index >= 0 && index <= virt_ids->capacity);
  return &virt_ids->entries[index];
}

inline mana_handle get_real_id(mana_handle virt_id) {
  return get_virt_id_entry(virt_ids, virt_id).real_id;
}

inline mana_handle get_virt_id_desc(mana_handle virt_id) {
  return get_virt_id_entry(virt_ids, virt_id).desc;
}

inline void update_virt_id(mana_handle virt_id,
                           mana_handle real_id, void *desc) {
  unsigned int index = virt_id >> VIRT_ID_MASK_LEN;
  assert(index >= 0 && index <= virt_ids->capacity);
  virt_ids->entries[index] = real_id;
  virt_ids->entries[index] = desc;
}

inline void free_virt_id(mana_handle virt_id) {
  unsigned int index = virt_id >> VIRT_ID_MASK_LEN;
  assert(index >= 0 && index <= virt_ids->capacity);
  int mask = virt_id & VIRT_ID_MASK;
  if (mask == COMM_MASK) {
    free_virt_id(virt_ids->entries[index].desc->group);
  } else if (mask == GROUP_MASK) {
    free(global_ranks);
  } // TODO: Other cases if needed.
  free(virt_ids->entries[index].desc);
  virt_ids->entries[index].used = false;
  virt_ids->size--;
}

extern virt_id_table virt_ids;

MPI_Comm new_virt_comm(MPI_Comm real_comm);
MPI_Group new_virt_group(MPI_Group real_group);
MPI_Op new_virt_op(MPI_Op real_op);
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype);
MPI_Request new_virt_request(MPI_Request real_request);
#endif // MANA_VIRTUAL_ID_H
