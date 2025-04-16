#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <elf.h>

#define ROUND_UP(x) ((unsigned long)((x) + (0x1000-1)) \
                     & ~(unsigned long)(0x1000-1))

extern char **environ; // also sometimes defined in unistd.h
extern char **__environ;

char *dbg_bottom_of_stack = NULL;
char *dbg_end_marker_addr = NULL;
char *dbg_env_strings_addr = NULL;
char *dbg_argv_strings_addr = NULL;
char *dbg_auxv_ptr_addr = NULL;
char *dbg_env_ptr_addr = NULL;
char *dbg_argv_ptr_addr = NULL;
char *dbg_argc_addr = NULL;
void debugStack(char *argc_ptr, char *argv_ptr) {
  printf("original argc_ptr: %p\n", argc_ptr);
  printf("original argv_ptr: %p\n", argv_ptr);
  printf("dbg_argc_addr: %p\n", dbg_argc_addr);
  printf("dbg_argv_ptr_addr: %p\n", dbg_argv_ptr_addr);
  printf("dbg_env_ptr_addr: %p [16-byte aligned]\n", dbg_env_ptr_addr);
  printf("dbg_auxv_ptr_addr: %p\n", dbg_auxv_ptr_addr);
  printf("dbg_argv_strings_addr: %p\n", dbg_argv_strings_addr);
  printf("dbg_env_strings_addr: %p\n", dbg_env_strings_addr);
  printf("dbg_end_marker_addr: %p\n", dbg_end_marker_addr);
  printf("dbg_bottom_of_stack: %p\n", dbg_bottom_of_stack );
  fflush(stdout);
}

// See: http://articles.manugarg.com/aboutelfauxiliaryvectors.html
//   (but unfortunately, that's for a 32-bit system)
// The kernel code is here (not recommended for a first reading):
//    https://github.com/torvalds/linux/blob/master/fs/binfmt_elf.c
// The spec is here:  https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
char *deepCopyStack(int argc, char **argv, char *argc_ptr, char *argv_ptr,
                   unsigned long dest_argc, char **dest_argv, char *dest_stack,
                   Elf64_auxv_t **auxv_ptr, void **stack_bottom) {
  if (((unsigned long)dest_stack & (16-1)) != 0) {
    fprintf(stderr,
      "deepCopyStack() passed dest_stack that's not 16-byte aligned.\n"
      " This violates x86_64 ABI; needed for Intel SSE support.\n");
    exit(1);
  }
  assert((long int)(argv[-1]) == argc);
  // $sp should be pointing to &(argv[-1])
  char *start_stack = (char *)&(argv[-1]);
  int i;
  for (i = 0; argv[i] != NULL; i++) ;
  char **orig_envp = &(argv[i+1]);
  for (i = 0; orig_envp[i] != NULL; i++) ;
  Elf64_auxv_t *auxv = (Elf64_auxv_t *)&(orig_envp[i+1]);

  // Allocate storage for dest_stack
  // ... Figure out env strings size for current __environ.
  int env_strings_size = 0;
  int env_strings_count = 0;
  for (i = 0; __environ[i] != NULL; i++) {
    env_strings_size += strlen(__environ[i]) + 1;
    env_strings_count += 1;
  }
  int env_ptr_size = (env_strings_count + 1) * sizeof(__environ[0]);
  char *env_ptr_addr = (char *)&__environ[0];
  char **new_env_ptrs = (char**)malloc(env_ptr_size);
  memset(new_env_ptrs, 0, env_ptr_size);
  char *argv_strings_addr = argv[0];
  int argv_strings_size = (argv[argc-1] + strlen(argv[argc-1]) + 1) - argv[0];
  int argv_ptr_size = (dest_argc + 1) * sizeof(argv[0]);
  char *argv_ptr_addr = (char *)&dest_argv[0];

  for (i = 0; auxv[i].a_type != AT_NULL; i++) {};
  assert(auxv[i].a_type == AT_NULL);
  int auxv_size = (char *)&auxv[i+1] - (char *)&auxv[0];
  char *auxv_addr = (char *)&auxv[0];

  char *top_of_stack = (char *)&argv[-1];
  assert( (auxv_size + env_ptr_size) % 8 == 0);
  // The fnc _start() may copy 'argc', but this is its original address.
  char *argc_ptr_original = argv_ptr - sizeof(char *);
  // This shows that for current stack (not the copied one), _start copied argc.
  int dest_stack_size = sizeof(NULL) /* end marker */ +
                        env_strings_size + argv_strings_size +
                        32 /* max.  padding is 32 for x86_64 */ +
                        auxv_size + env_ptr_size + argv_ptr_size +
                        (auxv_size + env_ptr_size) % 16 /* 16-byte aligned */ +
                        (argv_ptr - argc_ptr_original);

  char *dest_top_of_stack =
    (char *)ROUND_UP((unsigned long)dest_stack + dest_stack_size);
  // (top_of_stack - start_stack) + extra in case __environ larger than environ
  size_t bottom_of_stack = ROUND_UP(top_of_stack - start_stack);
  char * dest_mem_addr = dest_top_of_stack - ROUND_UP(dest_stack_size);
                         
  // FIXME:  Consider replacing MAP_FIXED by MAP_FIXED_NOREPLACE
  //   and then checking that:  rc2 == dest_mem_addr
  int dest_mem_len = dest_stack_size;
  void *rc2 = mmap(dest_mem_addr - sysconf(_SC_PAGESIZE),
                   dest_mem_len + sysconf(_SC_PAGESIZE),
                   PROT_READ|PROT_WRITE|PROT_EXEC,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|MAP_GROWSDOWN,
                   -1, 0);
  *stack_bottom = rc2 + dest_mem_len + sysconf(_SC_PAGESIZE);
  if (rc2 == MAP_FAILED) { perror("mmap"); exit(1); }

/*************************************************************
 * We have now mmapp'ed the new stack.  Next, we have to populate it:
 * We will copy to the new stack with the last item of this table
 *  (the highest address), and then move to the lower addresses.
 * This way, we can add the strings in order, and later add the pointers.
 *
 *   stack pointer ->  [ argc = number of args ]     4
 *                   [ argv[0] (pointer) ]         4   (program name)
 *                   [ argv[1] (pointer) ]         4
 *                   [ argv[..] (pointer) ]        4 * x
 *                   [ argv[n - 1] (pointer) ]     4
 *                   [ argv[n] (pointer) ]         4   (= NULL)
 *
 *                   [ envp[0] (pointer) ]         4   (16-byte aligned)
 *                   [ envp[1] (pointer) ]         4
 *                   [ envp[..] (pointer) ]        4
 *                   [ envp[term] (pointer) ]      4   (= NULL)
 *
 *                   [ auxv[0] (Elf32_auxv_t) ]    8   (8-byte aligned)
                        [ 8-byte aligned, even though sizeof(Elf64_auxv_t)==16 ]
 *                   [ auxv[1] (Elf32_auxv_t) ]    8
 *                   [ auxv[..] (Elf32_auxv_t) ]   8
 *                   [ auxv[term] (Elf32_auxv_t) ] 8   (= AT_NULL vector)
 *
 *                   [ padding ]                   0 - 16
 *
 *                   [ argument ASCIIZ strings ]   >= 0
 *                   [ environment ASCIIZ str. ]   >= 0
 *
 * (0xbffffffc)      [ end marker ]                4   (= NULL)
 * (0xc0000000)      < bottom of stack >           0   (virtual)
 *
 *************************************************************/

  char *dest_curr_stack = dest_mem_addr;
dbg_bottom_of_stack = dest_curr_stack;

 /*****************************
  * end marker (null)
  *****************************/
  dest_curr_stack -= sizeof(NULL);
  assert(NULL == (void *)0x0);
  memset(dest_curr_stack, 0, sizeof(NULL));
dbg_end_marker_addr = dest_curr_stack;

 /*****************************
  * environment strings
  *****************************/
  dest_curr_stack -= env_strings_size;
  for (i = 0; __environ[i] != NULL; i++) {
    strcpy( dest_curr_stack, __environ[i] );
    // Change UH_PRELOAD to LD_PRELOAD
    if (strstr(dest_curr_stack, "UH_PRELOAD")) {
      dest_curr_stack[0] = 'L';
      dest_curr_stack[1] = 'D';
    }
    new_env_ptrs[i] = dest_curr_stack;
    dest_curr_stack += strlen(dest_curr_stack) + 1;
  }
  assert(__environ[i] == NULL);
  dest_curr_stack -= env_strings_size;
  environ = __environ = (char **)dest_curr_stack;
  // ld.so will probably reset environ and __environ anyway
dbg_env_strings_addr = dest_curr_stack;

 /*****************************
  * argv strings
  *****************************/
  dest_curr_stack -= argv_strings_size;
  memcpy(dest_curr_stack, argv_strings_addr, argv_strings_size);
  // ld.so will probably reset environ and __environ anyway
dbg_argv_strings_addr = dest_curr_stack;

 /*****************************
  * padding
  *****************************/
  // Note that in Intel x86_64, the stack pointer must be 16-byte aligned
  //   in order to properly support Intel SSE instructions that operate
  //   on 16-byte quantities (128-bit quantities).
  // Up to 32 bytes of padding before strings
  // And strings must be 16-byte aligned.
  // Round down to make it 16-byte aligned (as in Linux kernel):
  dest_curr_stack -= 32;
  dest_curr_stack =
           (char *)((unsigned long)dest_curr_stack | (unsigned long)0xf) - 0xf;

 /*****************************
  * auxv
  *****************************/
  dest_curr_stack -= auxv_size;
  // env_ptr_addr must be 16-byte aligned, and auxv_ptr 8-byte aligned:
  assert( (auxv_size + env_ptr_size) % 8 == 0);
  dest_curr_stack -= (auxv_size + env_ptr_size) % 16;
  // Save this now, and return at end of function.  This is an OUT parameter.
  Elf64_auxv_t *dest_auxv_ptr = (Elf64_auxv_t *)dest_curr_stack;
  memcpy(dest_curr_stack, auxv_addr, auxv_size);
dbg_auxv_ptr_addr = dest_curr_stack;

 /*****************************
  * environment pointers
  *****************************/
  // We copied __environ to dest_stack earlier.  So, we're pointing to stack.
  // We use the "padded" variable so that it will be 16-byte aligned.
  dest_curr_stack -= env_ptr_size;
  // We can't just copy from env_ptr_addr, to dest_curr_stack because
  // env_ptr_addr is an array of pointers to the _old_ stack.  We need
  // new_env_ptrs, an array pointing to the _new_ stack.
  memcpy(dest_curr_stack, new_env_ptrs, env_ptr_size);
  assert( (uint64_t)dest_curr_stack % 16 == 0 ); // env is 16-byte aligned.
  assert( *(char **)((char *)env_ptr_addr+env_ptr_size - sizeof(__environ[0]))
                     == NULL );
  free(new_env_ptrs);
dbg_env_ptr_addr = dest_curr_stack;

 /*****************************
  * argv pointers
  *****************************/
  dest_curr_stack -= argv_ptr_size;
  memcpy(dest_curr_stack, argv_ptr_addr, argv_ptr_size);
  assert( *(char **)((char *)argv_ptr_addr+argv_ptr_size - sizeof(argv[0]))
                    == NULL );
dbg_argv_ptr_addr = dest_curr_stack;

 /*****************************
  * argc (The fnc _start() may copy 'argc', but this is its original address.)
  *****************************/
  dest_curr_stack -= sizeof(char *);
  *(long int *)dest_curr_stack = dest_argc;
dbg_argc_addr = dest_curr_stack;

  // We're reseting __environ and environ now, but we shouldn't need to do this.
  // ld.so will do this again.
  __environ = environ = (char **)env_ptr_addr;

#ifdef DEBUG
debugStack(argc_ptr, argv_ptr);
#endif
  *auxv_ptr = dest_auxv_ptr;
#ifdef STANDALONE
printf("\nDEBUGGING:\ndest_mem_addr (top of stack): %p;\n"
       "dest_mem_addr - dest_stack_size: %p; (dest_stack_size: %p)\n"
       "dest_curr_stack: %p\n",
       dest_mem_addr, dest_mem_addr - dest_stack_size, dest_stack_size,
       dest_curr_stack);
#endif
  return dest_curr_stack;
}

// FIXME:  This was copied into lower-half.cpp, and is unused here.
//         Why didn't we simply remove the 'static' declaration?
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
  fprintf(stderr, "*** deepCopyStack:  TEST 1\n");
  char* dest_argv_orig[] = {"test_word1", "test_word2", "test_word3", NULL};
  Elf64_auxv_t *auxv_ptr;
  int dest_argc = sizeof(dest_argv_orig)/sizeof(dest_argv_orig[0]) - 1;
  char *dest_stack =
    deepCopyStack(argc, argv, (char *)&argc, (char *)&argv[0], dest_argc, dest_argv_orig, (char *)0x800000, &auxv_ptr);
  // 5: 3+1 words in argv, and one word in argc
  int env_padding = ((uint64_t)dest_stack + (dest_argc+2)*sizeof(char *)) % 8 / 8;
  int i;
  for (i = 0; envp[i] != NULL; i++) {
    assert(strcmp(((char **)dest_stack)[(dest_argc+2)+env_padding+i], envp[i]) == 0);
  }

  fprintf(stderr, "\n*** deepCopyStack:  TEST 2\n");
  char* dest_argv_orig2[] = {"test_word1", "test_word2", "test_word3", "test_word4", NULL};
  dest_argc = sizeof(dest_argv_orig2)/sizeof(dest_argv_orig2[0]) - 1;
  dest_stack =
    deepCopyStack(argc, argv, (char *)&argc, (char *)&argv[0], dest_argc, dest_argv_orig2, (char *)0x800000, &auxv_ptr);
  env_padding = ((uint64_t)dest_stack + (dest_argc+2)*sizeof(char *)) % 8 / 8;
  for (i = 0; envp[i] != NULL; i++) {
    assert(strcmp(((char **)dest_stack)[(dest_argc+2)+env_padding+i], envp[i]) == 0);
  }

  fprintf(stderr, "\n*** deepCopyStack:  dest_stack is a copy of stack.\n");
  return 0;
}
#endif
