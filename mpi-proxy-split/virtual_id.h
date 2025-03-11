#ifndef MANA_VIRTUAL_ID_H
#define MANA_VIRTUAL_ID_H

#include <mpi.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>

#define MANA_COMM_KIND 1
#define MANA_GROUP_KIND 2
#define MANA_DATATYPE_KIND 3
#define MANA_OP_KIND 4
#define MANA_REQUEST_KIND 5
#define MANA_FILE_KIND 6
#define MANA_VIRT_ID_KIND_SHIFT 28

extern MPI_Group g_world_group;
extern MPI_Comm g_world_comm;

typedef union {
  int _handle;
  int64_t _handle64;
  MPI_Comm comm;
  MPI_Group group;
  MPI_Request request;
  MPI_Op op;
  MPI_Datatype datatype;
  MPI_File file;
} mana_mpi_handle;

typedef struct {
  int size;
  int rank;
  int *global_ranks;
} mana_group_desc;

typedef struct {
  int size;
  int rank;
  int *global_ranks;
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
  // It's hard to decode and reconstruct "double derived datatypes",
  // which means datatypes that are created using derived datatypes.
  // So we decided to use the old record-and-replay approach to
  // reconstruct datatypes at restart. Therefore, there's no data
  // needs to be saved in the descriptor. We keep this structure
  // definition for future use.
} mana_datatype_desc;

typedef struct {
  // Use the g_param for restoring files for now.
  // Migarate codes to here later.
} mana_file_desc;

typedef struct {
  mana_mpi_handle real_id;
  void *desc;
} virt_id_entry;

extern std::map<int, virt_id_entry*> virt_ids;
typedef std::map<int, virt_id_entry*>::iterator virt_id_iterator;

extern int g_world_rank;

void init_predefined_virt_ids();
MPI_Comm new_virt_comm(MPI_Comm real_comm);
MPI_Group new_virt_group(MPI_Group real_group);
MPI_Op new_virt_op(MPI_Op real_op);
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype);
MPI_Request new_virt_request(MPI_Request real_request);
MPI_File new_virt_file(MPI_File real_request);

int is_predefined_id(mana_mpi_handle id);
mana_mpi_handle add_virt_id(mana_mpi_handle real_id, void *desc, int kind);
virt_id_entry* get_virt_id_entry(mana_mpi_handle virt_id);
mana_mpi_handle get_real_id(mana_mpi_handle virt_id);
void* get_virt_id_desc(mana_mpi_handle virt_id);
void free_desc(void *desc, int kind);
void free_virt_id(mana_mpi_handle virt_id);
void update_virt_id(mana_mpi_handle virt_id, mana_mpi_handle real_id);

void reconstruct_descriptors();
void init_predefined_virt_ids();
#endif // MANA_VIRTUAL_ID_H
