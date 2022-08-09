#ifndef WINDOW_H
#define WINDOW_H

#include <pthread.h>

#include <map>

#define MAX_SHM_WINDOWS 1000

typedef struct ShmWindowProperties {
  int shmid;
  int shm_size;
  void *shm_baseptr;
  void *shm_win_baseptr;
  int win_size;
  MPI_Comm comm;
  MPI_Aint local_size;
  int local_disp_unit;
  void *local_baseptr;
  MPI_Win local_virtual_win;
} ShmWindowProperties;

// Window
std::map<int, long> virtualAllocateWindowVsBasePtrMap;
std::map<int, int> virtualWindowVsVirtualCommMap;

// Shared Memory Window
static int sharedWindowCounter = -1;
static pthread_mutex_t sharedWindowCounterLock = PTHREAD_MUTEX_INITIALIZER;
std::map<int, int> virtualSharedWindowVsIndex;
ShmWindowProperties sharedWindowProperties[MAX_SHM_WINDOWS];

#endif // ifndef WINDOW_H
