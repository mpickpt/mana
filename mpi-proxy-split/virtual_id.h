#ifndef MANA_VIRTUAL_ID_H
#define MANA_VIRTUAL_ID_H

#include <mpi.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <vector>

#define MANA_COMM_KIND 1
#define MANA_GROUP_KIND 2
#define MANA_DATATYPE_KIND 3
#define MANA_OP_KIND 4
#define MANA_REQUEST_KIND 5
#define MANA_FILE_KIND 6
#define MANA_NUM_KINDS 6
#define MANA_VIRT_ID_KIND_SHIFT 28

// A virtual handle is a 32-bit value laid out as
//   bits 31..28  kind (MANA_*_KIND)
//   bits 27..24  generation of the table slot
//   bits 23..0   index of the slot in the virtual-ID table
// Freed slots are reused.  The generation changes on every reuse, so that a
// stale handle to a reused slot is detected instead of silently translated.
// A new handle never equals an upper-half predefined constant (MPI_COMM_WORLD,
// MPI_INT, ...): add_virt_id() skips such values.
#define MANA_VIRT_ID_GEN_SHIFT 24
#define MANA_VIRT_ID_GEN_MASK 0xf
#define MANA_VIRT_ID_SLOT_MASK 0xffffff
// The table is an array of fixed-size chunks that are never moved or freed,
// so entries have stable addresses.
#define MANA_VIRT_ID_CHUNK_SHIFT 12
#define MANA_VIRT_ID_CHUNK_SIZE (1 << MANA_VIRT_ID_CHUNK_SHIFT)
#define MANA_VIRT_ID_NUM_CHUNKS \
  ((MANA_VIRT_ID_SLOT_MASK + 1) >> MANA_VIRT_ID_CHUNK_SHIFT)

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
  // The Collective Clock's state of this communicator (see seq_num.cpp):
  // the global id of its group, and the group's entries in seq_num and
  // target, which all communicators of the same group share.
  unsigned int ggid;
  unsigned long *seq_num;
  unsigned long *target;
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

typedef enum __mpi_req
{
  UNKNOW_REQUEST,
  ISEND_REQUEST,
  IRECV_REQUEST,
  IBCAST_REQUEST,
  IREDUCE_REQUEST,
  IBARRIER_REQUEST,
} mpi_req_t;

// Struct to store the metadata of an nonblocking MPI send/recv call
typedef struct __mpi_nonblocking_call
{
  // control data
  mpi_req_t type;  // See enum __mpi_req
  // request parameters
  const void *sendbuf;
  void *recvbuf;
  int count;        // Count of data items
  MPI_Datatype datatype;  // Data type
  MPI_Comm comm;    // MPI communicator
  int remote_node;  // Can be dest or source depending on the call type
  int tag;          // MPI message tag
} mpi_nonblocking_call_t;

typedef struct virt_id_entry {
  mana_mpi_handle real_id;
  void *desc;
  uint64_t seq;       // Creation order; restart reconstructs in this order
  int virt;           // The virtual handle while in use; 0 if the slot is free
  int next_free;      // Next slot in the free list, if the slot is free
  unsigned int gen;   // Generation of the next handle that uses this slot
  // For a request of a pending MPI_Isend/MPI_Irecv: the call, linked in
  // posting order with the other pending calls (see p2p_log_replay.cpp).
  // Otherwise, call.type is UNKNOW_REQUEST.
  mpi_nonblocking_call_t call;
  struct virt_id_entry *pending_prev;
  struct virt_id_entry *pending_next;
  // True for the request of a non-blocking collective, which a checkpoint
  // completes first (see complete_pending_nonblocking_collectives()).
  bool collective;
} virt_id_entry;

// Synchronization: the table lives in upper-half memory and is saved in the
// checkpoint image.  Only application threads allocate and free slots
// (add_virt_id/free_virt_id); MANA does not support MPI_THREAD_MULTIPLE, so
// at most one does so at a time.  The checkpoint thread also translates
// handles and calls update_virt_id() during PRESUSPEND (the P2P drain) and
// at restart, possibly while an application thread is running.  Lookups are
// therefore lock-free: a chunk is published only after it is initialized,
// chunks never move, and an entry's 'virt' field is published after its
// other fields.
extern virt_id_entry *virt_id_chunks[MANA_VIRT_ID_NUM_CHUNKS];

extern int g_world_rank;

void init_predefined_virt_ids();
MPI_Comm new_virt_comm(MPI_Comm real_comm);
MPI_Group new_virt_group(MPI_Group real_group);
MPI_Op new_virt_op(MPI_Op real_op);
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype);
MPI_Request new_virt_request(MPI_Request real_request);
MPI_Request new_virt_collective_request(MPI_Request real_request);
MPI_File new_virt_file(MPI_File real_request);

int is_predefined_id(mana_mpi_handle id);
mana_mpi_handle add_virt_id(mana_mpi_handle real_id, void *desc, int kind);
virt_id_entry* get_virt_id_entry(mana_mpi_handle virt_id);
mana_mpi_handle get_real_id_slow(mana_mpi_handle virt_id);
void* get_virt_id_desc(mana_mpi_handle virt_id);
void free_desc(void *desc, int kind);
void free_virt_id(mana_mpi_handle virt_id);
void update_virt_id(mana_mpi_handle virt_id, mana_mpi_handle real_id);
size_t virt_id_live_count();
// Returns the virtual communicators in use, in creation order.  The
// checkpoint thread may call it while an application thread runs.
std::vector<MPI_Comm> live_virt_comms();
// Returns the requests of the non-blocking collectives that have not
// completed yet, in creation order.
std::vector<MPI_Request> pending_collective_requests();

void reconstruct_descriptors();
void init_predefined_virt_ids();

// Returns the table entry of a virtual handle that is in use, or NULL if
// 'virt_id' is not one (a predefined constant, a freed or stale handle, or
// garbage).  O(1), no locking.
static inline virt_id_entry*
lookup_virt_id_entry(mana_mpi_handle virt_id)
{
  unsigned int handle = (unsigned int)virt_id._handle;
  unsigned int kind = handle >> MANA_VIRT_ID_KIND_SHIFT;
  if (kind - 1 >= MANA_NUM_KINDS) {
    return NULL;
  }
  unsigned int slot = handle & MANA_VIRT_ID_SLOT_MASK;
  virt_id_entry *chunk =
    __atomic_load_n(&virt_id_chunks[slot >> MANA_VIRT_ID_CHUNK_SHIFT],
                    __ATOMIC_ACQUIRE);
  if (chunk == NULL) {
    return NULL;
  }
  virt_id_entry *entry = &chunk[slot & (MANA_VIRT_ID_CHUNK_SIZE - 1)];
  if (__atomic_load_n(&entry->virt, __ATOMIC_ACQUIRE) != virt_id._handle) {
    return NULL;
  }
  return entry;
}

// Translates a virtual handle, or an upper-half predefined constant, to the
// real handle of the lower half.
static inline mana_mpi_handle
get_real_id(mana_mpi_handle virt_id)
{
  virt_id_entry *entry = lookup_virt_id_entry(virt_id);
  if (entry != NULL) {
    mana_mpi_handle real_id;
    real_id._handle64 =
      __atomic_load_n(&entry->real_id._handle64, __ATOMIC_RELAXED);
    return real_id;
  }
  return get_real_id_slow(virt_id);
}
#endif // MANA_VIRTUAL_ID_H
