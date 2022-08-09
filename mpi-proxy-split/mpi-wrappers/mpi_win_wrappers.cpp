#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/types.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include <mpi.h>

#include "mpi_plugin.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_nextfunc.h"
#include "record-replay.h"
#include "virtual-ids.h"
#include "two-phase-algo.h"
#include "p2p_drain_send_recv.h"
#include "../window.h"

USER_DEFINED_WRAPPER(int, Alloc_mem, (MPI_Aint) size, (MPI_Info) info,
                     (void *) baseptr)
{
  // Since memory allocated by the lower half will be discarded during
  // checkpoint, we need to translate MPI_Alloc_mem and MPI_Free_mem
  // to malloc and free. This may slow down the program.
  DMTCP_PLUGIN_DISABLE_CKPT();
  *(void**)baseptr = malloc(size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return MPI_SUCCESS;
}

USER_DEFINED_WRAPPER(int, Free_mem, (void *) baseptr)
{
  // Since memory allocated by the lower half will be discarded during
  // checkpoint, we need to translate MPI_Alloc_mem and MPI_Free_mem
  // to malloc and free. This may slow down the program.
  DMTCP_PLUGIN_DISABLE_CKPT();
  free(baseptr);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return MPI_SUCCESS;
}

using namespace dmtcp_mpi;

USER_DEFINED_WRAPPER(int, Win_get_info, (MPI_Win) win, (MPI_Info *) info_used)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_get_info)(realWin, info_used);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_set_info, (MPI_Win) win, (MPI_Info) info)
{
  MPI_Comm comm = virtualWindowVsVirtualCommMap[win];
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Win_set_info)(realWin, info);
    RETURN_TO_UPPER_HALF();
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Win_create, (void *) base, (MPI_Aint) size,
                     (int) disp_unit, (MPI_Info) info, (MPI_Comm) comm,
                     (MPI_Win *) win)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval =
      NEXT_FUNC(Win_create)(base, size, disp_unit, info, realComm, win);
    RETURN_TO_UPPER_HALF();
    if (retval == MPI_SUCCESS && MPI_LOGGING()) {
      MPI_Win virtualWin = ADD_NEW_WIN(*win);
      *win = virtualWin;
      LOG_CALL(restoreWindows, Win_create, (long)base, size, disp_unit, info,
               comm, virtualWin);
      virtualWindowVsVirtualCommMap[virtualWin] = comm;
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Win_create_dynamic, (MPI_Info) info, (MPI_Comm) comm,
                     (MPI_Win *) win)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Win_create_dynamic)(info, realComm, win);
    RETURN_TO_UPPER_HALF();
    if (retval == MPI_SUCCESS && MPI_LOGGING()) {
      MPI_Win virtualWin = ADD_NEW_WIN(*win);
      *win = virtualWin;
      LOG_CALL(restoreWindows, Win_create_dynamic, info, comm, virtualWin);
      virtualWindowVsVirtualCommMap[virtualWin] = comm;
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Win_allocate, (MPI_Aint) size, (int) disp_unit,
                     (MPI_Info) info, (MPI_Comm) comm, (void *) baseptr,
                     (MPI_Win *) win)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    retval = MPI_Alloc_mem(size, info, baseptr);
    JASSERT(retval == MPI_SUCCESS)
      .Text("MPI_Win_allocate: Failed to allocate memory.");
    retval =
      MPI_Win_create(*(void **)baseptr, size, disp_unit, info, comm, win);
    virtualAllocateWindowVsBasePtrMap[*win] = (long)(*(void **)baseptr);
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

int
validate_win_allocate_shared_parameters(MPI_Comm comm)
{
  int retval;
  int *baseptr;
  MPI_Win win;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);

  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_allocate_shared)(1, 1, MPI_INFO_NULL, realComm,
                                          &baseptr, &win);
  RETURN_TO_UPPER_HALF();
  JASSERT(retval == MPI_SUCCESS)
    .Text("Win_allocate_shared: Unable to create dummy window object.");

  // Window should be of flavor MPI_WIN_FLAVOR_SHARED.
  int flag = -1;
  int *create_kind;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval =
    NEXT_FUNC(Win_get_attr)(win, MPI_WIN_CREATE_FLAVOR, &create_kind, &flag);
  RETURN_TO_UPPER_HALF();
  JASSERT(retval == MPI_SUCCESS)
    .Text("Win_allocate_shared: Unable to get window attribute.");
  return (*create_kind == MPI_WIN_FLAVOR_SHARED) ? 1 : 0;
}

USER_DEFINED_WRAPPER(int, Win_allocate_shared, (MPI_Aint) size, (int) disp_unit,
                     (MPI_Info) info, (MPI_Comm) comm, (void *) baseptr,
                     (MPI_Win *) win)
{
  std::function<int()> realBarrierCb = [=]() {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JASSERT(validate_win_allocate_shared_parameters(comm) == 1)
      .Text("Win_allocate_shared: Unable to create window, check parameters.");

    int retval;

    int max_size;
    retval = MPI_Allreduce(&size, &max_size, 1, MPI_INT, MPI_MAX, comm);
    JASSERT(retval == MPI_SUCCESS)
      .Text("Win_allocate_shared: Failed to get maximum shared memory window "
            "size.");

    int comm_size;
    retval = MPI_Comm_size(comm, &comm_size);
    JASSERT(retval == MPI_SUCCESS)
      .Text("Win_allocate_shared: Failed to get the size of <comm>.");

    int comm_rank;
    retval = MPI_Comm_rank(comm, &comm_rank);
    JASSERT(retval == MPI_SUCCESS)
      .Text("Win_allocate_shared: Failed to get the rank of the process in "
            "<comm>.");

    pthread_mutex_lock(&sharedWindowCounterLock);
    sharedWindowCounter++;
    pthread_mutex_unlock(&sharedWindowCounterLock);

    char key[MPI_MAX_INFO_KEY];
    if (comm_rank == 0) {
      int world_rank;
      retval = MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
      snprintf(key, sizeof(key), "/WIN-ALLOCATE-SHARED-%d-%d", world_rank,
               sharedWindowCounter);
      JASSERT(strlen(key) < sizeof(key))(key)(sizeof(key));
    }
    MPI_Bcast(key, sizeof(key), MPI_CHAR, 0, comm);

#if 0
    volatile int dummy = 1;
    while (dummy) {};
#endif

    if (comm_rank != 0) {
      retval = MPI_Barrier(comm);
    }

    // Create shared memory segment
    int win_size = max_size;
    int shm_size = comm_size * (win_size + sizeof(ShmWindowProperties));
    int shmid = shmget(ftok(key, 1), shm_size, 0644 | IPC_CREAT);
    JASSERT(shmid != -1)
      .Text("Win_allocate_shared: shmget(O_CREAT) - Unable to create shared "
            "memory segment.");

    if (comm_rank == 0) {
      retval = MPI_Barrier(comm);
    }

    // Attach to the segment to get a pointer to it.
    void *shm_baseptr = shmat(shmid, NULL, 0);
    JASSERT(shm_baseptr != (void *)-1)
      .Text("Win_allocate_shared: shmat() - Unable to attach to shared memory "
            "segment.");

    // Calculate base pointer values
    void *shm_win_baseptr =
      shm_baseptr + (comm_size * sizeof(ShmWindowProperties));
    *(void **)baseptr = shm_win_baseptr + (comm_rank * win_size);

    // Create window
    retval =
      MPI_Win_create(*(void **)baseptr, size, disp_unit, info, comm, win);

    sharedWindowProperties[sharedWindowCounter].shmid = shmid;
    sharedWindowProperties[sharedWindowCounter].shm_size = shm_size;
    sharedWindowProperties[sharedWindowCounter].shm_baseptr = shm_baseptr;
    sharedWindowProperties[sharedWindowCounter].shm_win_baseptr =
      shm_win_baseptr;
    sharedWindowProperties[sharedWindowCounter].win_size = win_size;
    sharedWindowProperties[sharedWindowCounter].comm = comm;
    sharedWindowProperties[sharedWindowCounter].local_size = size;
    sharedWindowProperties[sharedWindowCounter].local_disp_unit = disp_unit;
    sharedWindowProperties[sharedWindowCounter].local_baseptr =
      *(void **)baseptr;
    sharedWindowProperties[sharedWindowCounter].local_virtual_win = *win;

    memcpy(shm_baseptr + (comm_rank * sizeof(ShmWindowProperties)),
           &sharedWindowProperties[sharedWindowCounter],
           sizeof(ShmWindowProperties));

    virtualSharedWindowVsIndex[*win] = sharedWindowCounter;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return MPI_SUCCESS;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Win_shared_query, (MPI_Win) win, (int) rank,
                     (MPI_Aint *) size, (int *) disp_unit, (void *) baseptr)
{
  int retval = MPI_ERR_WIN;
  DMTCP_PLUGIN_DISABLE_CKPT();

  if (virtualSharedWindowVsIndex.find(win) !=
      virtualSharedWindowVsIndex.end()) {
    int index = virtualSharedWindowVsIndex[win];
    void *shm_baseptr = sharedWindowProperties[index].shm_baseptr;
    ShmWindowProperties swp;
    memcpy(&swp, shm_baseptr + (rank * sizeof(ShmWindowProperties)),
           sizeof(ShmWindowProperties));
    *size = swp.local_size;
    *disp_unit = swp.local_disp_unit;
    *(void **)baseptr = swp.local_baseptr;
    retval = MPI_SUCCESS;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_free, (MPI_Win *) win)
{
  int retval = MPI_ERR_WIN;
  DMTCP_PLUGIN_DISABLE_CKPT();

  if (virtualAllocateWindowVsBasePtrMap.find(*win) !=
      virtualAllocateWindowVsBasePtrMap.end()) {
    MPI_Free_mem((void *)virtualAllocateWindowVsBasePtrMap[*win]);
    *win = MPI_WIN_NULL;
    retval = MPI_SUCCESS;
  } else if (virtualSharedWindowVsIndex.find(*win) !=
             virtualSharedWindowVsIndex.end()) {
    int index = virtualSharedWindowVsIndex[*win];
    int shmid = sharedWindowProperties[index].shmid;
    const void *shmaddr = sharedWindowProperties[index].shm_baseptr;
    if (shmdt(shmaddr) != -1 && shmctl(shmid, IPC_RMID, 0) != -1) {
      *win = MPI_WIN_NULL;
      retval = MPI_SUCCESS;
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_fence, (int) assert, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_fence)(assert, realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_flush, (int) rank, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_flush)(rank, realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_lock_all, (int) assert, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_lock_all)(assert, realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_sync, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_sync)(realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Win_unlock_all, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Win_unlock_all)(realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Get, (void *) origin_addr, (int) origin_count,
                     (MPI_Datatype) origin_datatype, (int) target_rank,
                     (MPI_Aint) target_disp, (int) target_count,
                     (MPI_Datatype) target_datatype, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval =
    NEXT_FUNC(Get)(origin_addr, origin_count, origin_datatype, target_rank,
                   target_disp, target_count, target_datatype, realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Put, (const void *) origin_addr, (int) origin_count,
                     (MPI_Datatype) origin_datatype, (int) target_rank,
                     (MPI_Aint) target_disp, (int) target_count,
                     (MPI_Datatype) target_datatype, (MPI_Win) win)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Win realWin = VIRTUAL_TO_REAL_WIN(win);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval =
    NEXT_FUNC(Put)(origin_addr, origin_count, origin_datatype, target_rank,
                   target_disp, target_count, target_datatype, realWin);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

PMPI_IMPL(int, MPI_Alloc_mem, MPI_Aint size, MPI_Info info, void *baseptr)
PMPI_IMPL(int, MPI_Free_mem, void *baseptr)

PMPI_IMPL(int, MPI_Win_get_info, MPI_Win win, MPI_Info *info_used) //
PMPI_IMPL(int, MPI_Win_set_info, MPI_Win win, MPI_Info info)

PMPI_IMPL(int, MPI_Win_create, void *base, MPI_Aint size, int disp_unit,
          MPI_Info info, MPI_Comm comm, MPI_Win *win)
PMPI_IMPL(int, MPI_Win_create_dynamic, MPI_Info info, MPI_Comm comm,
          MPI_Win *win)
PMPI_IMPL(int, MPI_Win_allocate, MPI_Aint size, int disp_unit, MPI_Info info,
          MPI_Comm comm, void *baseptr, MPI_Win *win)
PMPI_IMPL(int, MPI_Win_allocate_shared, MPI_Aint size, int disp_unit,
          MPI_Info info, MPI_Comm comm, void *baseptr, MPI_Win *win)

PMPI_IMPL(int, MPI_Win_shared_query, MPI_Win win, int rank, MPI_Aint *size,
          int *disp_unit, void *baseptr) //
PMPI_IMPL(int, MPI_Win_free, MPI_Win *win) //
PMPI_IMPL(int, MPI_Win_fence, int assert, MPI_Win win) //
PMPI_IMPL(int, MPI_Win_flush, int rank, MPI_Win win) //
PMPI_IMPL(int, MPI_Win_lock_all, int assert, MPI_Win win)
PMPI_IMPL(int, MPI_Win_sync, MPI_Win win) //
PMPI_IMPL(int, MPI_Win_unlock_all, MPI_Win win)

PMPI_IMPL(int, MPI_Get, void *origin_addr, int origin_count,
          MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp,
          int target_count, MPI_Datatype target_datatype, MPI_Win win)
PMPI_IMPL(int, MPI_Put, const void *origin_addr, int origin_count,
          MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp,
          int target_count, MPI_Datatype target_datatype, MPI_Win win);