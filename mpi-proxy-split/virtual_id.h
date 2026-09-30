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
  // A name of the communicator that is the same on all its members, since
  // its virtual handle is not (see unblockPendingRecvs()): a hash of its
  // global ranks in order, and how many communicators with the same hash
  // this process created before it.
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

// A derived datatype: the call that made it, with its arguments, so that
// restart can make it again (reconstruct_descriptors()), in creation order,
// from the old types it was made from.  BLACS makes and frees a datatype
// around almost every message, so descriptors come from a free list
// (alloc_datatype_desc()), and nothing else is allocated for the common
// constructors.
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
  // One reference from the application until it frees the datatype, and
  // one from each datatype made from it that is still referenced: restart
  // needs a freed datatype to make those again.
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
  // For a request of a pending MPI_Isend/MPI_Irecv: the call, linked in
  // posting order with the other pending calls (see p2p_log_replay.cpp).
  // Otherwise, call.type is UNKNOW_REQUEST.
  mpi_nonblocking_call_t call;
  struct virt_id_entry *pending_prev;
  struct virt_id_entry *pending_next;
  // True for the request of a non-blocking collective, which a checkpoint
  // completes first (see complete_pending_nonblocking_collectives()).
  bool collective;
  // For such a request: the thread that may use the real request now (see
  // claim_request()).
  int claim;
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
// Returns an empty descriptor for a new datatype.  The caller fills in the
// call that made the datatype, and passes it to new_virt_datatype().
mana_datatype_desc* alloc_datatype_desc();
MPI_Datatype new_virt_datatype(MPI_Datatype real_datatype,
                               mana_datatype_desc *desc);
// Records that the application committed the datatype.
void commit_virt_datatype(MPI_Datatype type);
// Called when the application frees the datatype.  Its virtual handle is
// released once no referenced datatype was made from it and no pending
// MPI_Isend/MPI_Irecv uses it.
void free_virt_datatype(MPI_Datatype type);
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
// Returns the communicator in use with the given name (see mana_comm_desc),
// or MPI_COMM_NULL if this process is not a member of it.  The checkpoint
// thread may call it while an application thread runs.
MPI_Comm find_virt_comm(uint64_t ranks_hash, unsigned int instance);

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

// A non-blocking collective's request may be tested both by the application
// (MPI_Test, MPI_Wait, ...) and, at checkpoint time, by the checkpoint thread
// (complete_pending_nonblocking_collectives()).  The two must not pass the
// same real request to the lower half at once: the call that completes it
// frees it.  So a thread claims the request first, and releases it after.
enum {
  REQUEST_UNCLAIMED,
  REQUEST_CLAIMED_BY_APPLICATION,
  REQUEST_CLAIMED_BY_CHECKPOINT
};

// Called by the application before it tests 'request'.  Returns false if the
// checkpoint thread is completing it; for the caller, the request is then not
// complete yet.  Other requests need no claim.
static inline bool
claim_request(MPI_Request request)
{
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = request});
  if (entry == NULL || !entry->collective) {
    return true;
  }
  int unclaimed = REQUEST_UNCLAIMED;
  return __atomic_compare_exchange_n(&entry->claim, &unclaimed,
                                     REQUEST_CLAIMED_BY_APPLICATION, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

// Releases the claim on 'request'.  (There is nothing to release once the
// request has been freed.)
static inline void
release_request(MPI_Request request)
{
  virt_id_entry *entry =
    lookup_virt_id_entry((mana_mpi_handle){.request = request});
  if (entry != NULL && entry->collective) {
    __atomic_store_n(&entry->claim, REQUEST_UNCLAIMED, __ATOMIC_RELEASE);
  }
}

// The checkpoint thread's claim_request(): waits while the application tests
// the request.  Returns false if the application has completed it meanwhile.
bool claim_request_for_checkpoint(MPI_Request request);
#endif // MANA_VIRTUAL_ID_H
