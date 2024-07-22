#ifndef MANA_VIRTUAL_ID_H
#define MANA_VIRTUAL_ID_H

#include <mpi.h>
#include <assert.h>
#include <stdlib.h>

#define COMM_KIND 1
#define GROUP_KIND 2
#define DATATYPE_KIND 3
#define OP_KIND 4
#define REQUEST_KIND 5
#define FILE_KIND 6
#define VIRT_ID_KIND_SHIFT 28

typedef union {
  int _handle;
  MPI_Comm comm;
  MPI_Group group;
  MPI_Request request;
  MPI_Op op;
  MPI_Datatype datatype;
  MPI_File file;
} mana_handle;

typedef struct {
  unsigned long seq_num;
  unsigned long target_num;
} mana_ggid_desc;

typedef struct {
  int size;
  int rank;
  int *global_ranks;
  int ref_count;
} mana_group_desc;

typedef struct {
  MPI_Group group; // virtual id of the group
  mana_group_desc *group_desc;
  mana_ggid_desc ggid_desc;
} mana_comm_desc;

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
  MPI_Datatype *datatypes;
  int combiner;
} mana_datatype_desc;

typedef struct {
  // FIXME: use the g_param for restoring files for now.
  // Migarate codes to here later.
} mana_file_desc;

typedef struct {
  mana_handle real_id;
  void *desc;
} virt_id_entry;

extern std::unordered_map<int, virt_id_entry*> virt_ids;
extern std::unordered_map<int, mana_ggid_desc*> ggid_table;
typedef std::unordered_map<int, virt_id_entry*>::iterator virt_id_iterator;

inline mana_handle add_virt_id(mana_handle real_id, void *desc, int kind) {
  static int next_id = 0;
  mana_handle new_virt_id;
  new_virt_id._handle = kind << VIRT_ID_KIND_SHIFT;
  new_virt_id._handle = new_virt_id._handle | next_id;
  next_id++;

  virt_id_entry *entry = (virt_id_entry*)malloc(sizeof(entry));
  entry->real_id = real_id;
  entry->desc = desc;
  virt_ids[new_virt_id._handle] = entry;

  return new_virt_id;
}

inline virt_id_entry* get_virt_id_entry(mana_handle virt_id) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  return it->second;
}

inline mana_handle get_real_id(mana_handle virt_id) {
  return get_virt_id_entry(virt_id)->real_id;
}

inline void* get_virt_id_desc(mana_handle virt_id) {
  return get_virt_id_entry(virt_id)->desc;
}

inline void free_desc(void *desc, int kind) {
  if (kind == COMM_KIND) {
    mana_comm_desc *comm_desc = (mana_comm_desc*)desc;
    free_desc(comm_desc->group_desc, GROUP_KIND);
  } else if (kind == GROUP_KIND) {
    mana_group_desc *group_desc = (mana_group_desc*)desc;
    free(group_desc->global_ranks);
  }
  free(desc);
}

inline void free_virt_id(mana_handle virt_id) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  int kind = virt_id._handle >> VIRT_ID_KIND_SHIFT;
  free_desc(it->second->desc, kind); // free descriptor
  free(it->second); // free virt_id_entry
}

inline void update_virt_id(mana_handle virt_id,
                           mana_handle real_id, void *desc) {
  virt_id_iterator it = virt_ids.find(virt_id._handle);
  // Should we abort or return an error code?
  if (it == virt_ids.end()) {
    fprintf(stderr, "Invalid MPI handle value: 0x%x\n", virt_id._handle);
    abort();
  }
  virt_id_entry *entry = it->second;
  int kind = virt_id._handle >> VIRT_ID_KIND_SHIFT;
  free_desc(entry->desc, kind); 
  entry->real_id = real_id;
  entry->desc = desc;
}

MPI_Comm new_virt_comm(MPI_Comm real_comm);
MPI_Group new_virt_group(MPI_Group real_group);
MPI_Op new_virt_op(MPI_Op real_op);
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype);
MPI_Request new_virt_request(MPI_Request real_request);
MPI_File new_virt_file(MPI_File real_request);
#endif // MANA_VIRTUAL_ID_H
