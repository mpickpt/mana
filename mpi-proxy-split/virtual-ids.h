#ifndef VIRTUAL_ID_H
#define VIRTUAL_ID_H

#include <mpi.h>

#define CONCAT(a,b) a ## b

// Writing these macros as ternary expressions means there is no overhead associated with extra function arguments.
#define DESC_TO_VIRTUAL(id, null) \ 
  (id == null) ? null : descriptorToVirtual(type)

#define VIRTUAL_TO_DESC(id, null) \
  (id == null) ? null : virtualToDescriptor(id)

// NOTE this operation is now accomplished effectively with DESCRIPTOR_TO_VIRTUAL.
// #define REAL_TO_VIRTUAL(id, null) \ 
//   (DESCRIPTOR_TO_VIRTUAL(id, null) == NULL) ? NULL : VIRTUAL_TO_DESCRIPTOR(id, null)

// HACK We use GNU macro extensions to store a temporary variable tmp.
#define VIRTUAL_TO_REAL(id, null) \
    {(id_desc_t* _tmp = VIRTUAL_TO_DESC(id, null); (tmp == NULL) ? NULL : tmp->real_id} 

#define ADD_NEW(real_id, null, descriptor_type)						\
    (real_id == null) ? null : assignVid((union id_desc_t*) CONCAT(init_,descriptor_type)(real_id))

#define REMOVE_OLD(virtual_id, null) \
  (virtual_id == null) ? null : onRemove(virtual_id)

#define UPDATE_MAP(virtual_id, real_id, null) \
  (virtual_id == null) ? null : updateMapping(virtual_id, real_id)/

#define DESC_TO_VIRTUAL_FILE(id) \
  DESC_TO_VIRTUAL(id, MPI_FILE_NULL) 
#define VIRTUAL_TO_DESC_FILE(id) \
  VIRTUAL_TO_DESC(id, MPI_FILE_NULL)
#define VIRTUAL_TO_REAL_FILE(id, null) \
  VIRTUAL_TO_REAL(id, null)
#define ADD_NEW_FILE(id) \
  ADD_NEW(id, MPI_FILE_NULL, virt_file_t)
#define REMOVE_OLD_FILE(id) \
  REMOVE_OLD(id, MPI_FILE_NULL)
#define UPDATE_FILE_MAP(v, r) \
  UPDATE_MAP(id, MPI_FILE_NULL)

#define DESC_TO_VIRTUAL_COMM(id) \
  DESC_TO_VIRTUAL(id, MPI_COMM_NULL) 
#define VIRTUAL_TO_DESC_COMM(id) \
  VIRTUAL_TO_DESC(id, MPI_COMM_NULL)
#define VIRTUAL_TO_REAL_COMM(id, null) \
  VIRTUAL_TO_REAL(id, MPI_COMM_NULL)
#define ADD_NEW_COMM(id) \
  ADD_NEW(id, MPI_COMM_NULL, comm_desc_t)
#define REMOVE_OLD_COMM(id) \
  REMOVE_OLD(id, MPI_COMM_NULL)
#define UPDATE_COMM_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_NULL)

#define DESC_TO_VIRTUAL_GROUP(id) \
  DESC_TO_VIRTUAL(id, MPI_GROUP_NULL) 
#define VIRTUAL_TO_DESC_GROUP(id) \
  VIRTUAL_TO_DESC(id, MPI_GROUP_NULL)
#define VIRTUAL_TO_REAL_GROUP(id) \
  VIRTUAL_TO_REAL(id, MPI_GROUP_NULL)
#define ADD_NEW_GROUP(id) \
  ADD_NEW(id, MPI_GROUP_NULL, virt_group_t)
#define REMOVE_OLD_GROUP(id) \
  REMOVE_OLD(id, MPI_GROUP_NULL)
#define UPDATE_GROUP_MAP(v, r) \
  UPDATE_MAP(id, MPI_GROUP_NULL)

#define DESC_TO_VIRTUAL_TYPE(id) \
  DESC_TO_VIRTUAL(id, MPI_TYPE_NULL) 
#define VIRTUAL_TO_DESC_TYPE(id) \
  VIRTUAL_TO_DESC(id, MPI_TYPE_NULL)
#define VIRTUAL_TO_REAL_TYPE(id) \
  VIRTUAL_TO_REAL(id, MPI_TYPE_NULL)
#define ADD_NEW_TYPE(id) \
  ADD_NEW(id, MPI_TYPE_NULL, virt_type_t)
#define REMOVE_OLD_TYPE(id) \
  REMOVE_OLD(id, MPI_TYPE_NULL)
#define UPDATE_TYPE_MAP(v, r) \
  UPDATE_MAP(id, MPI_TYPE_NULL)

#define DESC_TO_VIRTUAL_OP(id) \
  DESC_TO_VIRTUAL(id, MPI_OP_NULL) 
#define VIRTUAL_TO_DESC_OP(id) \
  VIRTUAL_TO_DESC(id, MPI_OP_NULL)
#define VIRTUAL_TO_REAL_OP(id) \
  VIRTUAL_TO_REAL(id, MPI_OP_NULL)
#define ADD_NEW_OP(id) \
  ADD_NEW(id, MPI_OP_NULL, virt_op_t)
#define REMOVE_OLD_OP(id) \
  REMOVE_OLD(id, MPI_OP_NULL)
#define UPDATE_OP_MAP(v, r) \
  UPDATE_MAP(id, MPI_OP_NULL)

#define DESC_TO_VIRTUAL_COMM_KEYVAL(id) \
  DESC_TO_VIRTUAL(id, MPI_COMM_KEYVAL_NULL) 
#define VIRTUAL_TO_DESC_COMM_KEYVAL(id) \
  VIRTUAL_TO_DESC(id, MPI_COMM_KEYVAL_NULL)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  VIRTUAL_TO_REAL(id, MPI_COMM_KEYVAL_NULL)
#define ADD_NEW_COMM_KEYVAL(id) \
  ADD_NEW(id, MPI_COMM_KEYVAL_NULL, virt_comm_keyval_t)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  REMOVE_OLD(id, MPI_COMM_KEYVAL_NULL)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  UPDATE_MAP(id, MPI_COMM_KEYVAL_NULL)

#define DESC_TO_VIRTUAL_REQUEST(id) \
  DESC_TO_VIRTUAL(id, MPI_REQUEST_NULL) 
#define VIRTUAL_TO_DESC_REQUEST(id) \
  VIRTUAL_TO_DESC(id, MPI_REQUEST_NULL)
#define VIRTUAL_TO_REAL_REQUEST(id) \
  VIRTUAL_TO_REAL(id, MPI_REQUEST_NULL)
#define ADD_NEW_REQUEST(id) \
  ADD_NEW(id, MPI_REQUEST_NULL, virt_request_t)
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

extern struct ggid_desc_t;
extern struct comm_desc_t;
extern struct group_desc_t;
extern struct request_desc_t;
extern struct op_desc_t;
extern struct datatype_desc_t;
extern union id_desc_t;

extern std::map<int, id_desc_t*> idDescriptorTable;
extern std::map<int, ggid_desc_t*> ggidDescriptorTable; 

long onRemove(int virtId);
int assignVid(id_desc_t* desc);
id_desc_t* updateMapping(int virtId, long realId);
id_desc_t* virtualToDescriptor(int virtId);
int descriptorToVirtual(id_desc_t* desc);
id_desc_t* init_id_desc_t();
datatype_desc_t* init_datatype_desc_t(MPI_Type realType);
op_desc_t* init_op_desc_t(MPI_Op realOp);
request_desc_t* init_request_desc_t(MPI_Request realReq);
group_desc_t* init_group_desc_t(MPI_Group realGroup);
comm_desc_t* init_comm_desc_t(MPI_Comm realComm);
int getggid(MPI_Comm comm);
int hash(int i);
