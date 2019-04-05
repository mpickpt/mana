#ifndef _MPI_COPYBITS_H
#define _MPI_COPYBITS_H

#include <stdint.h>
#include <ucontext.h>
#include <link.h>
#include <unistd.h>

#include "libproxy.h"

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL , void *
# define MAIN_AUXVEC_PARAM , auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif // ifdef MAIN_AUXVEC_ARG

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))
#define ROUND_DOWN(addr) ((unsigned long)addr & ~(getpagesize()-1))

// The transient proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
struct LowerHalfInfo_t
{
  void *startTxt;
  void *endTxt;
  void *startData;
  void *endOfHeap;
  void *libc_start_main;
  void *main;
  void *libc_csu_init;
  void *libc_csu_fini;
  void *fsaddr;
  uint64_t lh_AT_PHNUM;
  uint64_t lh_AT_PHDR;
  void *g_appContext;
  void *lh_dlsym;
  void *getRankFptr;
  void *parentStackStart;
  void *updateEnvironFptr;
  void *getMmappedListFptr;
  void *resetMmappedListFptr;
  void *memRange;
};

typedef struct __MemRange
{
  void *start;
  void *end;
} MemRange_t;

typedef struct __MmapInfo
{
  void *addr;
  size_t len;
  int unmapped;
  int guard;
} MmapInfo_t;

extern struct LowerHalfInfo_t info;
extern int g_numMmaps;
extern MmapInfo_t *g_list;
extern MemRange_t *g_range;

extern int main(int argc, char *argv[], char *envp[]);
extern int __libc_csu_init (int argc, char **argv, char **envp);
extern void __libc_csu_fini (void);
extern MmapInfo_t* getMmappedList(int *num);
extern void resetMmappedList();

typedef int (*mainFptr)(int argc, char *argv[], char *envp[]);
typedef void (*finiFptr) (void);

extern int __libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                             int argc,
                             char **argv,
                             __typeof (main) init,
                             void (*fini) (void),
                             void (*rtld_fini) (void),
                             void *stack_end);

typedef int (*libcFptr_t) (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
                           int ,
                           char **,
                           __typeof (main) ,
                           void (*fini) (void),
                           void (*rtld_fini) (void),
                           void *);

typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
typedef void* (*updateEnviron_t)(char **environ);
typedef MmapInfo_t* (*getMmappedList_t)(int *num);
typedef void (*resetMmappedList_t)();

#endif // #ifndef _MPI_COPYBITS_H
