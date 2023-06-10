#ifndef VIRTUAL_ID_H
#define VIRTUAL_ID_H

#include <mpi.h>

#define CONCAT(a,b) a ## b

// Writing these macros as ternary expressions means there is no overhead associated with extra function arguments.
#define DESC_TO_VIRTUAL(id, null) \
  (id == null) ? null : descriptorToVirtual(type)

#define VIRTUAL_TO_DESC(id, null, desc_type)					\
  (id == null) ? NULL : ((desc_type*) virtualToDescriptor( *((int*)(&id)) ))

// NOTE this operation is now accomplished effectively with DESCRIPTOR_TO_VIRTUAL.
// #define REAL_TO_VIRTUAL(id, null) \ 
//   (DESCRIPTOR_TO_VIRTUAL(id, null) == NULL) ? NULL : VIRTUAL_TO_DESCRIPTOR(id, null)

#define VIRTUAL_TO_REAL(id, null, real_id_type, desc_type)    \
    ({                                              \
      id_desc_t* _VTR_tmp = VIRTUAL_TO_DESC(id, null, desc_type);			\
       real_id_type _VTR_id = (_VTR_tmp == NULL) ? null : ((desc_type *)_VTR_tmp)->real_id; \
       _VTR_id; \
     })

#define ADD_NEW(real_id, null, real_id_type, descriptor_type)		\
  (real_id == null) ? null : ((real_id_type)assignVid((union id_desc_t*) CONCAT(init_,descriptor_type)(real_id)))

#define REMOVE_OLD(virtual_id, null) \
  (virtual_id == null) ? null : onRemove(virtual_id)

#define UPDATE_MAP(virtual_id, real_id, null) \
  (virtual_id == null) ? null : updateMapping(virtual_id, real_id)/

#define DESC_TO_VIRTUAL_FILE(id) \
  DESC_TO_VIRTUAL(id, MPI_FILE_NULL) 
#define VIRTUAL_TO_DESC_FILE(id) \
  VIRTUAL_TO_DESC(id, MPI_FILE_NULL, file_desc_t)
#define VIRTUAL_TO_REAL_FILE(id) \
  VIRTUAL_TO_REAL(id, MPI_FILE_NULL, MPI_File, file_desc_t)
#define ADD_NEW_FILE(id) \
  ADD_NEW(id, MPI_FILE_NULL, MPI_File, file_desc_t)
#define REMOVE_OLD_FILE(id) \
  REMOVE_OLD(id, MPI_FILE_NULL)
#define UPDATE_FILE_MAP(v, r) \
  UPDATE_MAP(id, MPI_FILE_NULL)

#define DESC_TO_VIRTUAL_COMM(id) \
  DESC_TO_VIRTUAL(id, MPI_COMM_NULL) 
#define VIRTUAL_TO_DESC_COMM(id) \
  VIRTUAL_TO_DESC(id, MPI_COMM_NULL, comm_desc_t)
#define VIRTUAL_TO_REAL_COMM(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_NULL, MPI_Comm, comm_desc_t)
#define ADD_NEW_COMM(id) \
  ADD_NEW(id, MPI_COMM_NULL, MPI_Comm, comm_desc_t)
#define REMOVE_OLD_COMM(id) \
  REMOVE_OLD(id, MPI_COMM_NULL)
#define UPDATE_COMM_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_NULL)

#define DESC_TO_VIRTUAL_GROUP(id) \
  DESC_TO_VIRTUAL(id, MPI_GROUP_NULL) 
#define VIRTUAL_TO_DESC_GROUP(id) \
  VIRTUAL_TO_DESC(id, MPI_GROUP_NULL, group_desc_t)
#define VIRTUAL_TO_REAL_GROUP(id) \
  VIRTUAL_TO_REAL(id, MPI_GROUP_NULL, MPI_Group, group_desc_t)
#define ADD_NEW_GROUP(id) \
  ADD_NEW(id, MPI_GROUP_NULL, MPI_Group, group_desc_t)
#define REMOVE_OLD_GROUP(id) \
  REMOVE_OLD(id, MPI_GROUP_NULL)
#define UPDATE_GROUP_MAP(v, r) \
  UPDATE_MAP(id, MPI_GROUP_NULL)

#define DESC_TO_VIRTUAL_TYPE(id) \
  DESC_TO_VIRTUAL(id, MPI_DATATYPE_NULL) 
#define VIRTUAL_TO_DESC_TYPE(id) \
  VIRTUAL_TO_DESC(id, MPI_DATATYPE_NULL, datatype_desc_t)
#define VIRTUAL_TO_REAL_TYPE(id) \
  VIRTUAL_TO_REAL(id, MPI_DATATYPE_NULL, MPI_Datatype, datatype_desc_t)
#define ADD_NEW_TYPE(id) \
  ADD_NEW(id, MPI_DATATYPE_NULL, MPI_Datatype, datatype_desc_t)
#define REMOVE_OLD_TYPE(id) \
  REMOVE_OLD(id, MPI_DATATYPE_NULL)
#define UPDATE_TYPE_MAP(v, r) \
  UPDATE_MAP(id, MPI_DATATYPE_NULL)

#define DESC_TO_VIRTUAL_OP(id) \
  DESC_TO_VIRTUAL(id, MPI_OP_NULL) 
#define VIRTUAL_TO_DESC_OP(id) \
  VIRTUAL_TO_DESC(id, MPI_OP_NULL, op_desc_t)
#define VIRTUAL_TO_REAL_OP(id) \
  VIRTUAL_TO_REAL(id, MPI_OP_NULL, MPI_Op, op_desc_t)
#define ADD_NEW_OP(id) \
  ADD_NEW(id, MPI_OP_NULL, MPI_Op, op_desc_t)
#define REMOVE_OLD_OP(id) \
  REMOVE_OLD(id, MPI_OP_NULL)
#define UPDATE_OP_MAP(v, r) \
  UPDATE_MAP(id, MPI_OP_NULL)

#define DESC_TO_VIRTUAL_COMM_KEYVAL(id) \
  DESC_TO_VIRTUAL(id, MPI_COMM_KEYVAL_NULL) 
#define VIRTUAL_TO_DESC_COMM_KEYVAL(id) \
  VIRTUAL_TO_DESC(id, MPI_COMM_KEYVAL_NULL, virt_comm_keyval_t)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_KEYVAL_NULL, MPI_Commkeyval, virt_comm_keyval_t)
#define ADD_NEW_COMM_KEYVAL(id) \
  ADD_NEW(id, MPI_COMM_KEYVAL_NULL, MPI_Commkeyval, virt_comm_keyval_t)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  REMOVE_OLD(id, MPI_COMM_KEYVAL_NULL)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_KEYVAL_NULL)

#define DESC_TO_VIRTUAL_REQUEST(id) \
  DESC_TO_VIRTUAL(id, MPI_REQUEST_NULL) 
#define VIRTUAL_TO_DESC_REQUEST(id) \
  VIRTUAL_TO_DESC(id, MPI_REQUEST_NULL, request_desc_t)
#define VIRTUAL_TO_REAL_REQUEST(id) \
  VIRTUAL_TO_REAL(id, MPI_REQUEST_NULL, MPI_Request, request_desc_t)
#define ADD_NEW_REQUEST(id) \
  ADD_NEW(id, MPI_REQUEST_NULL, MPI_Request, request_desc_t)
#define REMOVE_OLD_REQUEST(id) \
  REMOVE_OLD(id, MPI_REQUEST_NULL)
#define UPDATE_REQUEST_MAP(v, r) \
  UPDATE_MAP(id, MPI_REQUEST_NULL)

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

/* struct ggid_desc_t; */
/* struct comm_desc_t; */
/* struct group_desc_t; */
/* struct request_desc_t; */
/* struct op_desc_t; */
/* struct datatype_desc_t; */
/* union id_desc_t; */

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
    MPI_Status status; // Real MPI status in the lower-half
};

struct op_desc_t {
    MPI_Op real_id; // Real MPI operator in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    MPI_User_function *user_fn; // Function pointer to the user defined op function
};

struct datatype_desc_t {
  // TODO add mpi type identifier field virtual class
    MPI_Datatype real_id; // Real MPI type in the lower-half
    int handle; // A copy of the int type handle generated from the address of this struct
    // Components of user-defined datatype.
    MPI_Count num_integers;
    int *integers;
    MPI_Count num_addresses;
    int *addresses;
    MPI_Count num_large_counts;
    int *large_counts;
    MPI_Count num_datatypes;
    int *datatypes;
    int *combiner;
};

struct file_desc_t {
  MPI_File real_id;
  int handle;
  // TODO We probably want to save something else too.
};

union id_desc_t {
    comm_desc_t comm;
    group_desc_t group;
    request_desc_t request;
    op_desc_t op;
    datatype_desc_t datatype;
    file_desc_t file;
  operator comm_desc_t () const { return comm; }
  operator group_desc_t () const { return group; }
  operator request_desc_t () const { return request; }
  operator op_desc_t () const { return op; }
  operator datatype_desc_t () const { return datatype; }
  operator file_desc_t () const { return file; }
};

extern std::map<int, id_desc_t*> idDescriptorTable;
extern std::map<int, ggid_desc_t*> ggidDescriptorTable; 
typedef typename std::map<int, id_desc_t*>::iterator id_desc_iterator;
typedef typename std::map<int, ggid_desc_t*>::iterator ggid_desc_iterator;
typedef std::pair<int, id_desc_t*> id_desc_pair;
typedef std::pair<int, ggid_desc_t*> ggid_desc_pair;

long onRemove(int virtId);
int assignVid(id_desc_t* desc);
id_desc_t* updateMapping(int virtId, long realId);
id_desc_t* virtualToDescriptor(int virtId);
int descriptorToVirtual(id_desc_t* desc);
id_desc_t* init_id_desc_t();
datatype_desc_t* init_datatype_desc_t(MPI_Datatype realType);
op_desc_t* init_op_desc_t(MPI_Op realOp);
request_desc_t* init_request_desc_t(MPI_Request realReq);
group_desc_t* init_group_desc_t(MPI_Group realGroup);
comm_desc_t* init_comm_desc_t(MPI_Comm realComm);
file_desc_t* init_file_desc_t(MPI_File realFile);
int getggid(MPI_Comm comm);
int hash(int i);
long virtual_to_real(id_desc_t* desc);

#endif // ifndef VIRTUAL_ID_H
