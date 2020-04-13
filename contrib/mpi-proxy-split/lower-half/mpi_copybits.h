#ifndef _MPI_COPYBITS_H
#define _MPI_COPYBITS_H

#include <stdint.h>
#include <ucontext.h>
#include <link.h>
#include <unistd.h>

#include "lower_half_api.h"

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

extern struct LowerHalfInfo_t info;
extern int g_numMmaps;
extern MmapInfo_t *g_list;
extern MemRange_t *g_range;

extern int main(int argc, char *argv[], char *envp[]);
extern int __libc_csu_init (int argc, char **argv, char **envp);
extern void __libc_csu_fini (void);

extern int __libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                             int argc,
                             char **argv,
                             __typeof (main) init,
                             void (*fini) (void),
                             void (*rtld_fini) (void),
                             void *stack_end);


#endif // #ifndef _MPI_COPYBITS_H
