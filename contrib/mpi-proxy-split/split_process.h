#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H

#ifndef _GNU_SOURCE
# define _GNU_SOURCE         /* See feature_test_macros(7) */
#endif
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <string.h>
#include <pthread.h>

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
// FIXME:  Use GLIBC:include/link.h -- l_tls_initimage_size initimage_size
//                             (or calc. it from phdr as in GLIBC:elf/dl-load.c)
// #define BUF_SIZE 1024
// #define BUF_SIZE 720
// #define BUF_SIZE 512
// #define BUF_SIZE 256
// #define BUF_SIZE 128
#define BUF_SIZE 64
// used part of tcb header:
// (gdb) p (char *) &((struct pthread *) $rsp)->header.__unused2 - $rsp
static const size_t TCB_HEADER_SIZE = 120;
static char *fsaddr_buf[BUF_SIZE + TCB_HEADER_SIZE];

static inline void SET_LOWER_HALF_FS_CONTEXT() {
  // Compute the upper-half and lower-half fs addresses
  if (!fsaddr_initialized) {
    fsaddr_initialized = 1;
    lh_fsaddr = lh_info.fsaddr - BUF_SIZE;
    syscall(SYS_arch_prctl, ARCH_GET_FS, &uh_fsaddr);
    uh_fsaddr = uh_fsaddr - BUF_SIZE;
  }
  memcpy(fsaddr_buf, uh_fsaddr, BUF_SIZE + TCB_HEADER_SIZE);
  memcpy(uh_fsaddr, lh_fsaddr, BUF_SIZE + TCB_HEADER_SIZE);

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
  ((void **)(uh_fsaddr + BUF_SIZE))[0] = (void *) (uh_fsaddr + BUF_SIZE);
}

static inline void RESTORE_UPPER_HALF_FS_CONTEXT() {
  memcpy(lh_fsaddr, uh_fsaddr, BUF_SIZE + TCB_HEADER_SIZE);
  memcpy(uh_fsaddr, fsaddr_buf, BUF_SIZE + TCB_HEADER_SIZE);

  // restore self pointer to original driver-half TLS location
  ((void **)(lh_fsaddr + BUF_SIZE))[0] = (void *) (lh_fsaddr + BUF_SIZE);
}

// ===================================================
// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef _SPLIT_PROCESS_H
