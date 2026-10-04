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
// A slot's generation changes on every reuse, so stale handles are detected.
// No handle equals an upper-half predefined constant: such slots are reserved.
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
  // The Cartesian topology, or cart_ndims == -1 if there is none.  Restart
  // makes it again without reordering, so each rank keeps its coordinates.
  int cart_ndims;
  int *cart_dims;
  int *cart_periods;
  // Collective Clock state (see seq_num.cpp): the group's global id and its
  // entries in seq_num and target, shared by all communicators of the group.
  unsigned int ggid;
  unsigned long *seq_num;
  unsigned long *target;
  // A name for the communicator that all its members agree on, unlike the
  // virtual handle, which differs between processes.  For a blocked
  // MPI_Recv, the P2P drain publishes this name, and the rank that sends
  // the dummy message finds its own handle with find_virt_comm().
  //   ranks_hash: a hash of the global ranks, in rank order.
  //   instance: how many communicators with the same ranks_hash this
  //     process created before this one.  new_virt_comm() takes it from
  //     comm_instances, a count per ranks_hash.  All members create a
  //     communicator in the same call, so they get the same instance.
  uint64_t ranks_hash;
  unsigned int instance;
} mana_comm_desc;

typedef struct {
  // For now, there's no need for additional informations.
  // NULL will be used in virt_id_entry of request.
} mana_request_desc;

typedef struct {
  MPI_User_function *user_fn;
  int commute;
} mana_op_desc;

// How a derived datatype was made (mana_datatype_desc.constructor).
enum {
  MANA_TYPE_CONTIGUOUS = 1,
  MANA_TYPE_VECTOR,
  MANA_TYPE_HVECTOR,
  MANA_TYPE_INDEXED,
  MANA_TYPE_HINDEXED,
  MANA_TYPE_STRUCT,
  MANA_TYPE_DUP,
  MANA_TYPE_RESIZED
};

// A derived datatype: the call that made it and its arguments, so that
// restart can make it again.  Descriptors come from a free list
// (alloc_datatype_desc()): BLACS makes and frees one around most messages.
typedef struct mana_datatype_desc {
  int constructor;              // MANA_TYPE_*
  int count;
  int blocklength;              // vector, hvector
  int stride;                   // vector, in elements
  MPI_Aint hstride;             // hvector, in bytes
  MPI_Aint lb;                  // resized
  MPI_Aint extent;              // resized
  MPI_Datatype oldtype;         // all but struct
  int *blocklengths;            // indexed, hindexed, struct: 'count'
  int *displacements;           // indexed
  MPI_Aint *hdisplacements;     // hindexed, struct
  MPI_Datatype *oldtypes;       // struct
  bool committed;
  // One reference from the application until it frees the datatype, and one
  // from each live datatype made from it: restart needs it to remake those.
  int refs;
  struct mana_datatype_desc *next_free;
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
  // For a pending MPI_Isend/MPI_Irecv: the call, linked in posting order
  // (see p2p_log_replay.cpp).  Otherwise, call.type is UNKNOW_REQUEST.
  mpi_nonblocking_call_t call;
  struct virt_id_entry *pending_prev;
  struct virt_id_entry *pending_next;
  // True for a non-blocking collective's request.
  bool collective;
  // Set by complete_virt_request(), with the receive's status.
  bool completed;
  MPI_Status status;
} virt_id_entry;

// Synchronization: only application threads allocate and free slots, one at
// a time (MANA does not support MPI_THREAD_MULTIPLE).  The checkpoint thread
// also translates handles and calls update_virt_id() (P2P drain, restart)
// while an application thread may run.  So lookups are lock-free: a chunk is
// published after it is initialized and never moves, and an entry's 'virt'
// is published after its other fields.
extern virt_id_entry *virt_id_chunks[MANA_VIRT_ID_NUM_CHUNKS];

extern int g_world_rank;

void init_predefined_virt_ids();
MPI_Comm new_virt_comm(MPI_Comm real_comm);
MPI_Group new_virt_group(MPI_Group real_group);
MPI_Op new_virt_op(MPI_Op real_op);
// Returns a zeroed descriptor; the caller fills it in and passes it to
// new_virt_datatype().
mana_datatype_desc* alloc_datatype_desc();
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype,
                               mana_datatype_desc *desc);
// Records that the application committed the datatype.
void commit_virt_datatype(MPI_Datatype type);
// Called when the application frees the datatype.  The handle is released
// once no live datatype made from it and no pending MPI_Isend/MPI_Irecv
// uses it.
void free_virt_datatype(MPI_Datatype type);
// Releases the freed datatypes that no pending MPI_Isend/MPI_Irecv uses any
// more.  Call it from an application thread after removing a pending call
// (clearPendingRequestFromLog()): the checkpoint thread must not free
// datatypes.
void release_freed_datatypes();
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
// Marks 'request', a receive that MANA completed for the application, as
// done: its real request becomes MPI_REQUEST_NULL, and the application's
// MPI_Wait/MPI_Test/MPI_Request_get_status return 'status'.
void complete_virt_request(MPI_Request request, const MPI_Status *status);
// If complete_virt_request() completed 'request', copies its status to
// 'status' (unless NULL) and returns true.
bool completed_request_status(MPI_Request request, MPI_Status *status);
size_t virt_id_live_count();
// Returns the communicators in use, in creation order.  Safe to call from
// the checkpoint thread while an application thread runs.
std::vector<MPI_Comm> live_virt_comms();
// Returns the incomplete non-blocking collective requests, in creation order.
std::vector<MPI_Request> pending_collective_requests();
// Returns the communicator with this name (see mana_comm_desc), or
// MPI_COMM_NULL.  Safe to call from the checkpoint thread while an
// application thread runs.
MPI_Comm find_virt_comm(uint64_t ranks_hash, unsigned int instance);

void reconstruct_descriptors();
void init_predefined_virt_ids();

// Returns the entry of a virtual handle in use, or NULL (a predefined
// constant, a freed or stale handle, or garbage).  Lock-free.
static inline virt_id_entry*
lookup_virt_id_entry(mana_mpi_handle virt_id)
{
  // Virtual handles have 32 bits: with 64-bit MPI handles (Open MPI), a
  // handle whose upper 32 bits are not 0 is not virtual.
  if (sizeof(MPI_Comm) > sizeof(int) &&
      ((uint64_t)virt_id._handle64 >> 32) != 0) {
    return NULL;
  }
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
