#ifndef VIRTUAL_ID_H
#define VIRTUAL_ID_H

#include <mpi.h>
#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "dmtcp.h"

#define CONCAT(a,b) a ## b

// num - type - VID MASK
// 0 - undefined - 0x00000000
// 1 - communicator - 0x01000000
// 2 - group - 0x02000000
// 3 - request - 0x03000000
// 4 - op - 0x04000000
// 5 - datatype - 0x05000000
// 6 - file - 0x06000000
// 7 - comm_keyval - 0x07000000

#define UNDEFINED_MASK 0x00000000
#define COMM_MASK 0x01000000
#define GROUP_MASK 0x02000000
#define REQUEST_MASK 0x03000000
#define OP_MASK 0x04000000
#define DATATYPE_MASK 0x05000000
#define FILE_MASK 0x06000000
#define COMM_KEYVAL_MASK 0x07000000

#define DESC_TO_VIRTUAL(desc, null, real_type)		\
  ({ \
    real_type _DTV_vId = (desc == NULL) ? null : desc->handle; \
   _DTV_vId; \
  })

#define VIRTUAL_TO_DESC(id, null, desc_type)					\
  (id == null) ? NULL : ((desc_type*) virtualToDescriptor( *((int*)(&id)) ))

#define VIRTUAL_TO_REAL(id, null, real_id_type, desc_type)    \
    ({                                              \
      desc_type* _VTR_tmp = VIRTUAL_TO_DESC(id, null, desc_type);			\
       real_id_type _VTR_id = (_VTR_tmp == NULL) ? id : _VTR_tmp->real_id; \
       _VTR_id; \
     })

#define ADD_NEW(real_id, null, real_id_type, descriptor_type, vid_mask)	\
  ({ \
    real_id_type _AD_retval; \
    descriptor_type* _AD_desc; \
    if (real_id != null) { \
        _AD_desc = CONCAT(init_,descriptor_type)(real_id);	\
        int _AD_vId = nextvId++; \
	_AD_vId = _AD_vId | vid_mask; \
        _AD_desc->handle = _AD_vId; \
        idDescriptorTable[_AD_vId] = ((union id_desc_t*) _AD_desc);	\
        _AD_retval = *((real_id_type*)&_AD_vId);						\
    } else { \
      _AD_retval = null; \
    } \
    _AD_retval; \
  })

#define REMOVE_OLD(virtual_id, null, descriptor_type, real_type)	\
  ({ \
    real_type _RO_retval; \
    if (virtual_id == null) { \
      _RO_retval = null; \
    } else { \
      descriptor_type* _RO_torem; \
      id_desc_iterator it = idDescriptorTable.find(*(int*)virtual_id); \
      if (it != idDescriptorTable.end()) { \
	_RO_torem = ((descriptor_type*)it->second);	   \
      } \
      idDescriptorTable.erase(*(int*)virtual_id); \
      _RO_retval = _RO_torem->real_id; \
      CONCAT(destroy_,descriptor_type)(_RO_torem); \
    }  \
    _RO_retval; \
  })

#define UPDATE_MAP(virtual_id, to_update, null, descriptor_type, to_update_type)	\
  ({ \
    id_desc_iterator _UM_it = idDescriptorTable.find(*((int*)(&virtual_id))); \
    to_update_type _UM_retval; \
    if (_UM_it != idDescriptorTable.end()) { \
      descriptor_type* desc = ((descriptor_type*)idDescriptorTable[*((int*)(&virtual_id))]); \
      desc->real_id = to_update; \
      _UM_retval = virtual_id; \
    } else { 		     \
      _UM_retval = null; \
    } \
    _UM_retval; \
})

#define DESC_TO_VIRTUAL_FILE(desc) \
  DESC_TO_VIRTUAL(desc, MPI_FILE_NULL, MPI_File) 
#define VIRTUAL_TO_DESC_FILE(id) \
  VIRTUAL_TO_DESC(id, MPI_FILE_NULL, file_desc_t)
#define VIRTUAL_TO_REAL_FILE(id) \
  VIRTUAL_TO_REAL(id, MPI_FILE_NULL, MPI_File, file_desc_t)
#define ADD_NEW_FILE(id) \
  ADD_NEW(id, MPI_FILE_NULL, MPI_File, file_desc_t, FILE_MASK)
#define REMOVE_OLD_FILE(id) \
  REMOVE_OLD(id, MPI_FILE_NULL, file_desc_t, MPI_File)
#define UPDATE_FILE_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_FILE_NULL, file_desc_t, MPI_File)

#define DESC_TO_VIRTUAL_COMM(desc) \
  DESC_TO_VIRTUAL(desc, MPI_COMM_NULL, MPI_Comm) 
#define VIRTUAL_TO_DESC_COMM(id) \
  VIRTUAL_TO_DESC(id, MPI_COMM_NULL, comm_desc_t)
#define VIRTUAL_TO_REAL_COMM(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_NULL, MPI_Comm, comm_desc_t); 
#define ADD_NEW_COMM(id) \
  ADD_NEW(id, MPI_COMM_NULL, MPI_Comm, comm_desc_t, COMM_MASK)
#define REMOVE_OLD_COMM(id) \
  REMOVE_OLD(id, MPI_COMM_NULL, comm_desc_t, MPI_Comm)
#define UPDATE_COMM_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_COMM_NULL, comm_desc_t, MPI_Comm)

#define DESC_TO_VIRTUAL_GROUP(desc) \
  DESC_TO_VIRTUAL(desc, MPI_GROUP_NULL, MPI_Group) 
#define VIRTUAL_TO_DESC_GROUP(id) \
  VIRTUAL_TO_DESC(id, MPI_GROUP_NULL, group_desc_t)
#define VIRTUAL_TO_REAL_GROUP(id) \
  VIRTUAL_TO_REAL(id, MPI_GROUP_NULL, MPI_Group, group_desc_t)
#define ADD_NEW_GROUP(id) \
  ADD_NEW(id, MPI_GROUP_NULL, MPI_Group, group_desc_t, GROUP_MASK)
#define REMOVE_OLD_GROUP(id) \
  REMOVE_OLD(id, MPI_GROUP_NULL, group_desc_t, MPI_Group)
#define UPDATE_GROUP_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_GROUP_NULL, group_desc_t, MPI_Group)

#define DESC_TO_VIRTUAL_TYPE(desc) \
  DESC_TO_VIRTUAL(desc, MPI_DATATYPE_NULL, MPI_Datatype) 
#define VIRTUAL_TO_DESC_TYPE(id) \
  VIRTUAL_TO_DESC(id, MPI_DATATYPE_NULL, datatype_desc_t)
#define VIRTUAL_TO_REAL_TYPE(id) \
  VIRTUAL_TO_REAL(id, MPI_DATATYPE_NULL, MPI_Datatype, datatype_desc_t)
#define ADD_NEW_TYPE(id) \
  ADD_NEW(id, MPI_DATATYPE_NULL, MPI_Datatype, datatype_desc_t, DATATYPE_MASK)
#define REMOVE_OLD_TYPE(id) \
  REMOVE_OLD(id, MPI_DATATYPE_NULL, datatype_desc_t, MPI_Datatype)
#define UPDATE_TYPE_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_DATATYPE_NULL, datatype_desc_t, MPI_Datatype)

#define DESC_TO_VIRTUAL_OP(desc) \
  DESC_TO_VIRTUAL(desc, MPI_OP_NULL, MPI_Op) 
#define VIRTUAL_TO_DESC_OP(id) \
  VIRTUAL_TO_DESC(id, MPI_OP_NULL, op_desc_t)
#define VIRTUAL_TO_REAL_OP(id) \
  VIRTUAL_TO_REAL(id, MPI_OP_NULL, MPI_Op, op_desc_t)
#define ADD_NEW_OP(id) \
  ADD_NEW(id, MPI_OP_NULL, MPI_Op, op_desc_t, OP_MASK) // TODO OP_create and use VIRT_TO_DESC.
#define REMOVE_OLD_OP(id) \
  REMOVE_OLD(id, MPI_OP_NULL, op_desc_t, MPI_Op)
#define UPDATE_OP_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_OP_NULL, op_desc_t, MPI_Op)

// TODO check with gene if we still need vid<->rid for comm_keyval
#define DESC_TO_VIRTUAL_COMM_KEYVAL(desc) \
  DESC_TO_VIRTUAL(desc, 0, int) 
#define VIRTUAL_TO_DESC_COMM_KEYVAL(id) \
  VIRTUAL_TO_DESC(id, 0, comm_keyval_desc_t)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  VIRTUAL_TO_REAL(id, 0, int, comm_keyval_desc_t)
#define ADD_NEW_COMM_KEYVAL(id) \
  ADD_NEW(id, 0, int, comm_keyval_desc_t, COMM_KEYVAL_MASK)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  REMOVE_OLD(id, 0, comm_keyval_desc_t, int)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  UPDATE_MAP(v, r, 0, comm_keyval_desc_t, int)

#define DESC_TO_VIRTUAL_REQUEST(desc) \
  DESC_TO_VIRTUAL(desc, MPI_REQUEST_NULL, MPI_Request) 
#define VIRTUAL_TO_DESC_REQUEST(id) \
  VIRTUAL_TO_DESC(id, MPI_REQUEST_NULL, request_desc_t)
#define VIRTUAL_TO_REAL_REQUEST(id) \
  VIRTUAL_TO_REAL(id, MPI_REQUEST_NULL, MPI_Request, request_desc_t)
#define ADD_NEW_REQUEST(id) \
  ADD_NEW(id, MPI_REQUEST_NULL, MPI_Request, request_desc_t, 0x03000000)
#define REMOVE_OLD_REQUEST(id) \
  REMOVE_OLD(id, MPI_REQUEST_NULL, request_desc_t, MPI_Request)
#define UPDATE_REQUEST_MAP(v, r) \
  UPDATE_MAP(v, r, MPI_REQUEST_NULL, request_desc_t, MPI_Request)

#ifndef NEXT_FUNC
# define NEXT_FUNC(func)                                                       \
  ({                                                                           \
    static __typeof__(&MPI_##func)_real_MPI_## func =                          \
                                                (__typeof__(&MPI_##func)) - 1; \
    if (_real_MPI_ ## func == (__typeof__(&MPI_##func)) - 1) {                 \
      _real_MPI_ ## func = (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);    \
    }                                                                          \
    _real_MPI_ ## func;                                                        \
  })
#endif // ifndef NEXT_FUNC

struct ggid_desc_t {
  int ggid; // hashing results of communicator members

  unsigned long seq_num;

  unsigned long target_num;
};

struct comm_desc_t {
    MPI_Comm real_id; // Real MPI communicator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    ggid_desc_t* ggid_desc; // A ggid_t structure, containing CVC information for this communicator.
    int size; // Size of this communicator
    int local_rank; // local rank number of this communicator
    int *ranks; // list of ranks of the group.

    // struct virt_group_t *group; // Or should this field be a pointer to virt_group_t?
};

struct group_desc_t {
    MPI_Group real_id; // Real MPI group in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    int size; // The size of this group in ranks.
    int *ranks; // list of ranks of the group.
    // unsigned int ggid; // Global Group ID
};

enum mpi_request_kind {
  COLLECTIVE,
  PEER_TO_PEER
};

struct request_desc_t {
    MPI_Request real_id; // Real MPI request in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    mpi_request_kind request_kind; // P2P request or collective request
    MPI_Status* status; // Real MPI status in the lower-half
};

struct op_desc_t {
    MPI_Op real_id; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
    int commute; // True if op is commutative.
};

struct datatype_desc_t {
  // TODO add mpi type identifier field virtual class
    MPI_Datatype real_id; // Real MPI type in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    // Components of user-defined datatype.
    int num_integers;
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
};

struct file_desc_t {
  MPI_File real_id;
  int handle;
  // TODO We probably want to save something else too.
};

struct comm_keyval_desc_t {
  int real_id;
  int handle;
}; // TODO VID keyval not needed at all

union id_desc_t {
    comm_desc_t comm;
    group_desc_t group;
    request_desc_t request;
    op_desc_t op;
    datatype_desc_t datatype;
    file_desc_t file;
    comm_keyval_desc_t comm_keyval;

  operator comm_desc_t () const { return comm; }
  operator group_desc_t () const { return group; }
  operator request_desc_t () const { return request; }
  operator op_desc_t () const { return op; }
  operator datatype_desc_t () const { return datatype; }
  operator file_desc_t () const { return file; }
  operator comm_keyval_desc_t () const { return comm_keyval; }
};

extern std::map<int, id_desc_t*> idDescriptorTable;
extern std::map<int, ggid_desc_t*> ggidDescriptorTable; 
extern int base;
extern int nextvId;
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;
typedef std::pair<int, id_desc_t*> id_desc_pair;
typedef std::pair<int, ggid_desc_t*> ggid_desc_pair;

id_desc_t* virtualToDescriptor(int virtId);

datatype_desc_t* init_datatype_desc_t(MPI_Datatype realType);
op_desc_t* init_op_desc_t(MPI_Op realOp);
request_desc_t* init_request_desc_t(MPI_Request realReq);
group_desc_t* init_group_desc_t(MPI_Group realGroup);
comm_desc_t* init_comm_desc_t(MPI_Comm realComm);
file_desc_t* init_file_desc_t(MPI_File realFile);

void destroy_datatype_desc_t(datatype_desc_t* datatype);
void destroy_op_desc_t(op_desc_t* op);
void destroy_request_desc_t(request_desc_t* request);
void destroy_group_desc_t(group_desc_t* group);
void destroy_comm_desc_t(comm_desc_t* comm);
void destroy_file_desc_t(file_desc_t* file);

void update_datatype_desc_t(datatype_desc_t* datatype);
void update_op_desc_t(op_desc_t* op, MPI_User_function* user_fn, int commute);
void update_request_desc_t(request_desc_t* request);
void update_group_desc_t(group_desc_t* group);
void update_comm_desc_t(comm_desc_t* comm);
void update_file_desc_t(file_desc_t* file);

void reconstruct_with_datatype_desc_t(datatype_desc_t* datatype);
void reconstruct_with_op_desc_t(op_desc_t* op);
void reconstruct_with_request_desc_t(request_desc_t* request);
void reconstruct_with_group_desc_t(group_desc_t* group);
void reconstruct_with_comm_desc_t(comm_desc_t* comm);
void reconstruct_with_file_desc_t(file_desc_t* file);

void update_descriptors();
void reconstruct_with_descriptors();

int getggid(MPI_Comm comm);
int hash(int i);

void init_comm_world(); // This is required for the CVC algorithm.

#endif // ifndef VIRTUAL_ID_H
