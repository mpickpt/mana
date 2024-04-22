#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <elf.h>
#include "mem_wrapper.h"

#define ROUND_UP(x) ((unsigned long)((x) + (0x1000-1)) \
                     & ~(unsigned long)(0x1000-1))

// See: http://articles.manugarg.com/aboutelfauxiliaryvectors.html
//   (but unfortunately, that's for a 32-bit system)
// The kernel code is here (not recommended for a first reading):
//    https://github.com/torvalds/linux/blob/master/fs/binfmt_elf.c
// The spec is here:  https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
char *deepCopyStack(int argc, char **argv,
                   unsigned long dest_argc, char **dest_argv, char *dest_stack,
                   Elf64_auxv_t **auxv_ptr) {
  if (((unsigned long)dest_stack & (16-1)) != 0) {
    fprintf(stderr,
      "deepCopyStack() passed dest_stack that's not 16-byte aligned.\n"
      " This violates x86_64 ABI; needed for Intel SSE support.\n");
    exit(1);
  }
  assert((unsigned long)(argv[-1]) == argc);
  // $sp should be pointing to &(argv[-1])
  char *start_stack = (char *)&(argv[-1]);
  char **envp;
  Elf64_auxv_t *auxv;
  int i; 
  for (i = 0; argv[i] != NULL; i++) ;
  envp = &(argv[i+1]);
  for (i = 0; envp[i] != NULL; i++) ;
  auxv = (Elf64_auxv_t *)&(envp[i+1]);
  for (i = 0; auxv[i].a_type != AT_NULL; i++) ;
  // argv and envp strings come next.
  // Up to 32 bytes of padding before strings
  // And strings must be 16-byte aligned.
  char *strings = (char *)&(auxv[i+1]) + 32; // Initial estimate
  // Round down to make it 16-byte aligned (as in Linux kernel):
  strings = argv[0];

  char *strings_ptr = strings;
  for ( ; strlen(strings_ptr) > 0; strings_ptr += strlen(strings_ptr)+1);
  assert(*(void **)strings_ptr == NULL);
  char *top_of_stack = strings_ptr + sizeof(void *);
  // FIXME:  We should use sysconf(PAGESIZE) below.
  assert(((unsigned long)top_of_stack & (0x1000-1)) == 0);
  // This is it!  It's the end of the stack memory region.

  // Compute storage requirements for dest_stack
  int argv_size_delta = (dest_argc - argc) * sizeof(argv[0]);
  int dest_argv_strings_size = 0;
  for (i = 0; i < dest_argc; i++) {
    dest_argv_strings_size += strlen(dest_argv[i]) + 1;
  }
  size_t dest_stack_size =
   (top_of_stack - start_stack) + argv_size_delta + dest_argv_strings_size;
  char *dest_top_of_stack =
    (char *)ROUND_UP((unsigned long)dest_stack + dest_stack_size);
  // ~(0x10 - 1) will round down to ensure 16-byte alignment of dest_stack
  // Required by Intel x86_64 ABI.
  dest_stack =
   (char *)((unsigned long)(dest_top_of_stack - dest_stack_size)
            & ~(0x10 - 1));

  // Allocate storage for dest_stack
  size_t dest_mem_len = ROUND_UP(dest_top_of_stack - dest_stack);
  char * dest_mem_addr = dest_top_of_stack - dest_mem_len;
  // FIXME:  Consider replacing MAP_FIXED by MAP_FIXED_NOREPLACE
  //   and then checking that:  rc2 == dest_mem_addr
  void *rc2 = mmap_wrapper(dest_mem_addr - sysconf(_SC_PAGESIZE),
                   dest_mem_len + sysconf(_SC_PAGESIZE),
                   PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|MAP_GROWSDOWN,
                   -1, 0);
  if (rc2 == MAP_FAILED) { perror("mmap"); exit(1); }

  // Now copy dest_argv[] strings into dest_strings pointer.
  // dest_argv_strings_size accounts for copying the extra strings into
  //   dest_stack, based the dest_argv.
  char *dest_strings =
    strings + (dest_top_of_stack - top_of_stack) - dest_argv_strings_size;
// FIXME:  Get dest_auxv, and change '+ 32' to make it 16-byte aligned.
// assert((char *)(&(dest_auxv[i+1])) + 32 < dest_strings);
  // Add strings of dest_argv[] to strings region above new stack
  char *new_dest_argv[100];
  assert(dest_argc < 100);
  int offset = 0;
  for (i = 0; i < dest_argc; i++) {
    new_dest_argv[i] = strcpy(dest_strings + offset, dest_argv[i]);
    offset += strlen(dest_argv[i]) + 1;
  }
  assert(dest_argv[i] == NULL);
  new_dest_argv[i] = dest_argv[i];  // Copy final NULL pointer

  // See FIXME above for what we need to make this part work.
  memcpy(dest_strings + dest_argv_strings_size,
         strings, top_of_stack - strings);

  // COPY start_stack to dest_stack:
  // Note that in Intel x86_64, the stack pointer must be 16-byte aligned
  //   in order to properly support Intel SSE instructions that operate
  //   on 16-byte quantities (128-bit quantities).
  *(unsigned long *)dest_stack = dest_argc;
  char **dest_argv_final = (char **)(dest_stack + sizeof(dest_argc));
  for (i = 0; new_dest_argv[i] != NULL; i++) {
    dest_argv_final[i] = new_dest_argv[i];
  }
  assert(new_dest_argv[i] == NULL);
  dest_argv_final[i] = new_dest_argv[i]; // Final NULL pointer of argv[]

  unsigned long delta = dest_strings + dest_argv_strings_size - strings;
  char **dest_envp = &(dest_argv_final[i+1]);
  for (i = 0; envp[i] != NULL; i++) {
    dest_envp[i] = envp[i] + delta;
  }
  assert(envp[i] == NULL);
  dest_envp[i] = envp[i]; // Final NULL pointer of envp[]

  Elf64_auxv_t *dest_auxv = (Elf64_auxv_t *)&(dest_envp[i+1]);
  if (auxv_ptr != NULL) {*auxv_ptr = dest_auxv;}
  for (i = 0; auxv[i].a_type != AT_NULL; i++) {
    dest_auxv[i] = auxv[i];
  }
  assert(auxv[i].a_type == AT_NULL);
  dest_auxv[i] = auxv[i]; // Final NULL pointer of argv[]
  // 32 bytes of padding before strings
  
  // Change "UH_PRELOAD" to "LD_PRELOAD". This way, upper half's ld.so
  // will preload the upper half wrapper library.
  char **newEnvPtr = (char**)dest_envp;
  for (; *newEnvPtr; newEnvPtr++) {
    if (strstr(*newEnvPtr, "LD_PRELOAD")) {
      fprintf(stderr, "LD_PRELOAD found\n");
    }
    if (strstr(*newEnvPtr, "UH_PRELOAD")) {
      (*newEnvPtr)[0] = 'L';
      (*newEnvPtr)[1] = 'D';
      break;
    }
  }

  return dest_stack;
}

// Given a pointer to auxvector, parses the aux vector and patches the AT_PHDR,
// AT_ENTRY, and AT_PHNUM entries.
static void
patchAuxv(Elf64_auxv_t *av, unsigned long phnum,
          unsigned long phdr, unsigned long entry)
{
  for (; av->a_type != AT_NULL; ++av) {
    switch (av->a_type) {
      case AT_PHNUM:
        av->a_un.a_val = phnum;
        break;
      case AT_PHDR:
        av->a_un.a_val = phdr;
        break;
      case AT_ENTRY:
        av->a_un.a_val = entry;
        break;
      default:
        break;
    }
  }
}

#ifdef STANDALONE
int main(int argc, char **argv, char **envp) {
  char* dest_argv_orig[] = {"test_word1", "test_word2", "test_word3", NULL};
  char *dest_stack =
    deepCopyStack(argc, argv, 3, dest_argv_orig, (char *)0x800000, NULL);
  char **dest_argv = (char **)(dest_stack + sizeof(argc));
  int i;
  for (i = 0; envp[i] != NULL; i++) {
    assert(strcmp(dest_argv[4+i], envp[i]) == 0);
  }
  fprintf(stderr, "deepCopyStack:  dest_stack is a copy of stack.\n");
  return 0;
}
#endif
