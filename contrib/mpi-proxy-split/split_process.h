#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H

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
// #define BUF_SIZE 1024
#define BUF_SIZE 720
// #define BUF_SIZE 512
// #define BUF_SIZE 256
// #define BUF_SIZE 128
// #define BUF_SIZE 64
static char *fsaddr_buf[BUF_SIZE];

static inline void SET_LOWER_HALF_FS_CONTEXT() {
  // Compute the upper-half and lower-half fs addresses
  if (!fsaddr_initialized) {
    fsaddr_initialized = 1;
    lh_fsaddr = lh_info.fsaddr - 64;
    uh_fsaddr = (void*)pthread_self() - 64;
  }
  memcpy(fsaddr_buf, uh_fsaddr, BUF_SIZE);
  memcpy(uh_fsaddr, lh_fsaddr, BUF_SIZE);
}

static inline void RESTORE_UPPER_HALF_FS_CONTEXT() {
  memcpy(uh_fsaddr, fsaddr_buf, BUF_SIZE);
}

// ===================================================
// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef _SPLIT_PROCESS_H
