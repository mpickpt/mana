#ifndef _LOWER_HALF_API_H
#define _LOWER_HALF_API_H

#include <stdint.h>

#include "libproxy.h"

#define GENERATE_ENUM(ENUM)    MPI_Fnc_##ENUM
#define GENERATE_FNC_PTR(FNC)  &MPI_##FNC
#define PAGE_SIZE              0x1000
#define HUGE_PAGE              0x200000

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL , void *
# define MAIN_AUXVEC_PARAM , auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif // ifdef MAIN_AUXVEC_ARG

#define ROUND_UP(addr)  \
    ((unsigned long)(addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define ROUND_DOWN(addr) ((unsigned long)addr & ~(PAGE_SIZE - 1))

#define ROUND_UP_HUGE(addr) \
    ((unsigned long)(addr + HUGE_PAGE - 1) & ~(HUGE_PAGE - 1))

// Shared data structures

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

enum MPI_Fncs {
  MPI_Fnc_NULL,
  FOREACH_FNC(GENERATE_ENUM)
  MPI_Fnc_Invalid,
};

// Useful type definitions

typedef int (*mainFptr)(int argc, char *argv[], char *envp[]);
typedef void (*finiFptr) (void);
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

// API

extern void *mydlsym(enum MPI_Fncs fnc);
extern int getRank();
extern void updateEnviron(const char **newenviron);
extern MmapInfo_t* getMmappedList(int *num);
extern void resetMmappedList();


#endif // ifndef _LOWER_HALF_API_H
