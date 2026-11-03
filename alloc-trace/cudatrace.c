/*
 * cudatrace: log every CUDA memory allocation, free and IPC call of a process.
 *
 * Loaded by the CUDA driver through CUDA_INJECTION64_PATH, which calls
 * InitializeInjection() at cuInit.  It subscribes to CUPTI driver API
 * callbacks, so it sees the driver calls made by a shared or a static cudart
 * (cudaMalloc -> cuMemAlloc_v2), by NCCL, and by the app itself.
 *
 * Output: $ALLOC_TRACE_DIR/cuda-<host>-<pid>.csv (default: current directory)
 *   t_ns,tid,dev,api,ptr,size,extra,status,caller
 * t_ns is CLOCK_MONOTONIC, the same clock mpitrace uses.  ptr is a device
 * address, a host address (host allocations) or a VMM handle (cuMemCreate,
 * cuMemRelease, export).  caller is the first library on the stack that is
 * not CUDA itself (set ALLOC_TRACE_CALLER=0 to skip the stack walk).
 */
#define _GNU_SOURCE
#include <cuda.h>
#include <cupti.h> // also declares the *_params structs
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *out;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int want_caller = 1;

static unsigned long long
now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// Basename of the first object on the stack that is not CUDA or this library.
static const char *
caller(void)
{
  static const char *skip[] = { "libcuda.", "libcudart", "libcupti",
                                "libcudatrace", NULL };
  void *pc[24];
  int n = backtrace(pc, 24);
  for (int i = 1; i < n; i++) {
    Dl_info info;
    if (!dladdr(pc[i], &info) || !info.dli_fname)
      continue;
    const char *base = strrchr(info.dli_fname, '/');
    base = base ? base + 1 : info.dli_fname;
    int ours = 0;
    for (int j = 0; skip[j]; j++)
      if (strstr(base, skip[j]))
        ours = 1;
    if (!ours)
      return *base ? base : "main";
  }
  return "?";
}

static void
emit(const CUpti_CallbackData *cb, const char *api, unsigned long long ptr,
     unsigned long long size, unsigned long long extra)
{
  uint32_t dev = (uint32_t)-1;
  if (cb->context)
    cuptiGetDeviceId(cb->context, &dev);
  CUresult status = *(CUresult *)cb->functionReturnValue;
  const char *who = want_caller ? caller() : "";
  pthread_mutex_lock(&lock);
  fprintf(out, "%llu,%d,%d,%s,%#llx,%llu,%#llx,%d,%s\n", now_ns(), gettid(),
          (int)dev, api, ptr, size, extra, (int)status, who);
  pthread_mutex_unlock(&lock);
}

#define P(type) const type##_params *p = (const type##_params *)cb->functionParams

static void CUPTIAPI
on_call(void *userdata, CUpti_CallbackDomain domain, CUpti_CallbackId id,
        const CUpti_CallbackData *cb)
{
  (void)userdata;
  (void)domain;
  // Results such as the new pointer exist only once the call has returned.
  if (cb->callbackSite != CUPTI_API_EXIT)
    return;
  const char *api = cb->functionName;

  switch (id) {
  // Device memory.
  case CUPTI_DRIVER_TRACE_CBID_cuMemAlloc_v2: {
    P(cuMemAlloc_v2);
    emit(cb, api, *p->dptr, p->bytesize, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocPitch_v2: {
    P(cuMemAllocPitch_v2);
    emit(cb, api, *p->dptr, *p->pPitch * p->Height, p->WidthInBytes);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocManaged: {
    P(cuMemAllocManaged);
    emit(cb, api, *p->dptr, p->bytesize, p->flags);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemFree_v2: {
    P(cuMemFree_v2);
    emit(cb, api, p->dptr, 0, 0);
    break;
  }

  // Stream-ordered allocations (cudaMallocAsync); extra is the pool.
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocAsync:
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocAsync_ptsz: {
    P(cuMemAllocAsync);
    emit(cb, api, *p->dptr, p->bytesize, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocFromPoolAsync:
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocFromPoolAsync_ptsz: {
    P(cuMemAllocFromPoolAsync);
    emit(cb, api, *p->dptr, p->bytesize, (unsigned long long)p->pool);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemFreeAsync:
  case CUPTI_DRIVER_TRACE_CBID_cuMemFreeAsync_ptsz: {
    P(cuMemFreeAsync);
    emit(cb, api, p->dptr, 0, 0);
    break;
  }

  // Host memory.
  case CUPTI_DRIVER_TRACE_CBID_cuMemAllocHost_v2: {
    P(cuMemAllocHost_v2);
    emit(cb, api, (unsigned long long)*p->pp, p->bytesize, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemHostAlloc: {
    P(cuMemHostAlloc);
    emit(cb, api, (unsigned long long)*p->pp, p->bytesize, p->Flags);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemFreeHost: {
    P(cuMemFreeHost);
    emit(cb, api, (unsigned long long)p->p, 0, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemHostRegister_v2: {
    P(cuMemHostRegister_v2);
    emit(cb, api, (unsigned long long)p->p, p->bytesize, p->Flags);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemHostUnregister: {
    P(cuMemHostUnregister);
    emit(cb, api, (unsigned long long)p->p, 0, 0);
    break;
  }

  // VMM.  ptr is the handle for cuMemCreate/cuMemRelease; extra is the
  // device the memory lives on, or the handle that cuMemMap maps.
  case CUPTI_DRIVER_TRACE_CBID_cuMemCreate: {
    P(cuMemCreate);
    emit(cb, api, *p->handle, p->size, p->prop->location.id);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemRelease: {
    P(cuMemRelease);
    emit(cb, api, p->handle, 0, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemAddressReserve: {
    P(cuMemAddressReserve);
    emit(cb, api, *p->ptr, p->size, p->alignment);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemAddressFree: {
    P(cuMemAddressFree);
    emit(cb, api, p->ptr, p->size, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemMap: {
    P(cuMemMap);
    emit(cb, api, p->ptr, p->size, p->handle);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemUnmap: {
    P(cuMemUnmap);
    emit(cb, api, p->ptr, p->size, 0);
    break;
  }

  // IPC: which buffers get shared with other processes.
  case CUPTI_DRIVER_TRACE_CBID_cuIpcGetMemHandle: {
    P(cuIpcGetMemHandle);
    emit(cb, api, p->dptr, 0, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuIpcOpenMemHandle_v2: {
    P(cuIpcOpenMemHandle_v2);
    emit(cb, api, *p->pdptr, 0, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuIpcCloseMemHandle: {
    P(cuIpcCloseMemHandle);
    emit(cb, api, p->dptr, 0, 0);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemExportToShareableHandle: {
    P(cuMemExportToShareableHandle);
    emit(cb, api, p->handle, 0, p->handleType);
    break;
  }
  case CUPTI_DRIVER_TRACE_CBID_cuMemImportFromShareableHandle: {
    P(cuMemImportFromShareableHandle);
    emit(cb, api, *p->handle, 0, p->shHandleType);
    break;
  }
  }
}

static const CUpti_CallbackId traced[] = {
  CUPTI_DRIVER_TRACE_CBID_cuMemAlloc_v2,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocPitch_v2,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocManaged,
  CUPTI_DRIVER_TRACE_CBID_cuMemFree_v2,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocAsync,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocAsync_ptsz,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocFromPoolAsync,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocFromPoolAsync_ptsz,
  CUPTI_DRIVER_TRACE_CBID_cuMemFreeAsync,
  CUPTI_DRIVER_TRACE_CBID_cuMemFreeAsync_ptsz,
  CUPTI_DRIVER_TRACE_CBID_cuMemAllocHost_v2,
  CUPTI_DRIVER_TRACE_CBID_cuMemHostAlloc,
  CUPTI_DRIVER_TRACE_CBID_cuMemFreeHost,
  CUPTI_DRIVER_TRACE_CBID_cuMemHostRegister_v2,
  CUPTI_DRIVER_TRACE_CBID_cuMemHostUnregister,
  CUPTI_DRIVER_TRACE_CBID_cuMemCreate,
  CUPTI_DRIVER_TRACE_CBID_cuMemRelease,
  CUPTI_DRIVER_TRACE_CBID_cuMemAddressReserve,
  CUPTI_DRIVER_TRACE_CBID_cuMemAddressFree,
  CUPTI_DRIVER_TRACE_CBID_cuMemMap,
  CUPTI_DRIVER_TRACE_CBID_cuMemUnmap,
  CUPTI_DRIVER_TRACE_CBID_cuIpcGetMemHandle,
  CUPTI_DRIVER_TRACE_CBID_cuIpcOpenMemHandle_v2,
  CUPTI_DRIVER_TRACE_CBID_cuIpcCloseMemHandle,
  CUPTI_DRIVER_TRACE_CBID_cuMemExportToShareableHandle,
  CUPTI_DRIVER_TRACE_CBID_cuMemImportFromShareableHandle,
};

static void
finish(void)
{
  pthread_mutex_lock(&lock);
  fflush(out);
  pthread_mutex_unlock(&lock);
}

int
InitializeInjection(void)
{
  const char *dir = getenv("ALLOC_TRACE_DIR");
  const char *c = getenv("ALLOC_TRACE_CALLER");
  char host[64] = "host", path[4096];
  gethostname(host, sizeof host);
  snprintf(path, sizeof path, "%s/cuda-%s-%d.csv", dir ? dir : ".", host,
           getpid());
  if (c && *c == '0')
    want_caller = 0;

  out = fopen(path, "w");
  if (!out) {
    perror(path);
    return 0;
  }
  setvbuf(out, NULL, _IOFBF, 1 << 20);
  fprintf(out, "t_ns,tid,dev,api,ptr,size,extra,status,caller\n");
  atexit(finish);

  CUpti_SubscriberHandle sub;
  if (cuptiSubscribe(&sub, (CUpti_CallbackFunc)on_call, NULL) !=
      CUPTI_SUCCESS) {
    fprintf(stderr, "cudatrace: cuptiSubscribe failed (another profiler?)\n");
    return 0;
  }
  for (size_t i = 0; i < sizeof traced / sizeof traced[0]; i++)
    cuptiEnableCallback(1, sub, CUPTI_CB_DOMAIN_DRIVER_API, traced[i]);
  return 1;
}
