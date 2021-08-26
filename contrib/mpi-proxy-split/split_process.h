#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H

#include <string.h>
#include <pthread.h>

#include "lower_half_api.h"

#define SET_FS_CONTEXT

// Helper class to save and restore context (in particular, the FS register),
// when switching between the upper half and the lower half. In the current
// design, the caller would generally be the upper half, trying to jump into
// the lower half. An example would be calling a real function defined in the
// lower half from a function wrapper defined in the upper half.
// Example usage:
//   int function_wrapper()
//   {
//     SwitchContext ctx;
//     return _real_function();
//   }
// The idea is to leverage the C++ language semantics to help us automatically
// restore the context when the object goes out of scope.
class SwitchContext
{
  private:
    unsigned long upperHalfFs; // The base value of the FS register of the upper half thread
    unsigned long lowerHalfFs; // The base value of the FS register of the lower half

  public:
    // Saves the current FS register value to 'upperHalfFs' and then
    // changes the value of the FS register to the given 'lowerHalfFs'
    explicit SwitchContext(unsigned long );

    // Restores the FS register value to 'upperHalfFs'
    ~SwitchContext();
};

// ===================================================
// FIXME: Fix this after the Linux 5.9 FSGSBASE patch
// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs) \
  do { \
    SwitchContext ctx((unsigned long)lhFs)

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF() \
  } while (0)

// ===================================================
// Workaround for quickly changing the fs address
static void* lh_fsaddr;
static void *uh_fsaddr;
static int fsaddr_initialized = 0;
// in glibc 2.26 for x86_64
// typedef struct
// {
//   void *tcb;            /* Pointer to the TCB.  Not necessarily the
//                            thread descriptor used by libpthread.  */
//   dtv_t *dtv;
//   void *self;           /* Pointer to the thread descriptor.  */
//   int multiple_threads;
//   int gscope_flag;
//   uintptr_t sysinfo;
//   uintptr_t stack_guard;
//   uintptr_t pointer_guard;
//   unsigned long int vgetcpu_cache[2];
// # ifndef __ASSUME_PRIVATE_FUTEX
//   int private_futex;
// # else
//   int __glibc_reserved1;
// # endif
//   int __glibc_unused1;
//   /* Reservation of some values for the TM ABI.  */
//   void *__private_tm[4];
//   /* GCC split stack support.  */
//   void *__private_ss;
//   long int __glibc_reserved2;
//   /* Must be kept even if it is no longer used by glibc since programs,
//      like AddressSanitizer, depend on the size of tcbhead_t.  */
//   __128bits __glibc_unused2[8][4] __attribute__ ((aligned (32)));
//
//   void *__padding[8];
// } tcbhead_t
// TODO: make this configuable
#ifndef LH_TLS_SIZE
/* readelf -S lh_proxy
 * [14] .tdata            PROGBITS         000000000e64d500  0044d500
 *      000000000000002c  0000000000000000 WAT       0     0     8
 * [15] .tbss             NOBITS           000000000e64d530  0044d52c
 *      0000000000000462  0000000000000000 WAT       0     0     8
 */
#define LH_TLS_SIZE 0x4a0
#endif
static const size_t TCB_HEADER_SIZE = 120; // offset of __glibc_reserved2
static char fsaddr_buf[LH_TLS_SIZE + TCB_HEADER_SIZE];

static inline void SET_LOWER_HALF_FS_CONTEXT() {
  // Compute the upper-half and lower-half fs addresses
  if (!fsaddr_initialized) {
    fsaddr_initialized = 1;
    lh_fsaddr = lh_info.fsaddr - LH_TLS_SIZE;
    uh_fsaddr = (char*)pthread_self() - LH_TLS_SIZE;
  }
  memcpy(fsaddr_buf, uh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);
  memcpy(uh_fsaddr, lh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);

  // on x86_64:
  // the tcb starts with 3 ptrs: self, dtv, header
  // self is used to get TLS variables
  // dtv is used to load tls for dynamically loaded libraries
  // header is used to access the tcb
  // self, header, and fs reg all point to the start of the tcb

  // we change the self pointed to point to the tcb at the new location
  // we leave the header pointer intact so that the tcb is modified in-place
  // at the old location

  // changing the location breaks things that keep pointers into the struct,
  // like linked lists. TCB has a lot of internal self-pointers and lists, so
  // it is advantageous to keep it in place. TLS usually has less (application
  // dependent), and must be moved because it can be accessed relative to fs
  // with no indirection. So we change self but not header.

  // change self pointer to new application half TLS location
  ((void **)(uh_fsaddr + LH_TLS_SIZE))[0] = (void *) (uh_fsaddr + LH_TLS_SIZE);
}

static inline void RESTORE_UPPER_HALF_FS_CONTEXT() {
  memcpy(lh_fsaddr, uh_fsaddr, LH_TLS_SIZE);
  memcpy(uh_fsaddr, fsaddr_buf, LH_TLS_SIZE + TCB_HEADER_SIZE);

  // restore self pointer to original driver-half TLS location
  ((void **)(lh_fsaddr + LH_TLS_SIZE))[0] = (void *) (lh_fsaddr + LH_TLS_SIZE);
}

// ===================================================
// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef _SPLIT_PROCESS_H
