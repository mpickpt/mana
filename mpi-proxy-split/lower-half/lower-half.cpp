#include <assert.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/personality.h>
#include <asm/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include "mtcp_header.h"
#include "mem-wrapper.h"
#include "patch-trampoline.h"
#include "lower-half-api.h"
#include "logging.h"
#include "dmtcp.h"
#include "dmtcprestartinternal.h"
#include "procmapsarea.h"
// FIXME:  lower-half should be a standalone program, except where it uses
//         mtcp_restart.  Why do we have all of these other DMTCP dependencies?
#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "util.h"

// kernel-loader.tgz (Gene Cooperman, gene@ccs.neu.edu) has upstream source
// for many files in this dir.  This file is kernel-loader.c at upstream.

// Uses ELF Format.  For background, read both of:
//  * http://www.skyfree.org/linux/references/ELF_Format.pdf
//  * /usr/include/elf.h
// Here's a fun blog with a gentle introduction to ELF:
//  * https://linux-audit.com/elf-binaries-on-linux-understanding-and-analysis/
//  * https://www.linuxjournal.com/article/1059 (early intro from 1995)
// The kernel code is here (not recommended for a first reading):
//    https://github.com/torvalds/linux/blob/master/fs/binfmt_elf.c

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args...) #args
#elif __i386__
# define CLEAN_FOR_64_BIT(args...) #args
#else
# define CLEAN_FOR_64_BIT(args...) "CLEAN_FOR_64_BIT_undefined"
#endif

#define PAGE_OFFSET(x) ((x)-ROUND_DOWN(x, PAGE_SIZE))
#define MAX_ELF_INTERP_SZ 256
#define LOADER_SIZE_LIMIT 0x2000000
#define MAX_CMD_ARGV2_LENGTH 100
#define HEAP_SIZE 0x1000000

// Lower half initialization helper functions
void set_addr_no_randomize(char *argv[]);
char *allocate_rtld_heap();
void initialize_lh_info();
void *lh_dlsym(enum MPI_Fncs fnc);
void write_lh_info_addr(int restore_mode);

// Restart Mode helper functions
int parse_restore_flag(int *argc, char **argv);
RestoreTarget* get_restore_target_info(DmtcpRestart &dmtcpRestart, int rank);
void validate_checkpoint_header(RestoreTarget *t, DmtcpCkptHeader &ckpt_hdr);
void reserve_memory_areas(RestoreTarget *t, off_t &ckpt_file_pos);
void *mmap_fixed_noreplace(void *addr, size_t length, int prot, int flags,
                           int fd, off_t offset);
void release_reserved_memory(RestoreTarget *t, off_t ckpt_file_pos);
void restore_session_leadership(RestoreTarget *t);
void restore_memory_data(RestoreTarget *t, DmtcpCkptHeader &ckpt_hdr);
static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr);

// Launch Mode helper functions
void parse_launch_arguments(int argc, char **argv, int *cmd_argc, char ***cmd_argv);
void print_usage_and_exit(char *prog_name);
void update_library_path(const char *argv0);
int prepend_to_library_path(const char *new_path);
void get_elf_interpreter(const char *cmd_path, Elf64_Addr *cmd_entry,
                          char elf_interpreter[]);
char *load_elf_interpreter(char elf_interpreter[],void *ld_so_addr, 
                            Elf64_Ehdr *ld_so_elf_hdr_ptr);
char *map_elf_interpreter_load_segment(int fd, Elf64_Phdr phdr,
                                      void *ld_so_addr);
void prepend_elf_interpreter_to_path(char **cmd_argv, int cmd_argc, char elf_interpreter[], char *cmd_argv2[]);
char *setup_upper_half_stack(int argc, char **argv, int cmd_argc, char **cmd_argv2,
                      char *interp_base_address, Elf64_auxv_t **auxv_ptr, void **stack_bottom);
// See: http://articles.manugarg.com/aboutelfauxiliaryvectors.html
//   (but unfortunately, that's for a 32-bit system)
char *deepCopyStack(int argc, char **argv, char *argc_ptr, char *argv_ptr,
                    unsigned long dest_argc, char **dest_argv, char *dest_stack,
                    Elf64_auxv_t **auxv_ptr, void **stack_bottom);
char *create_red_zone(char *stack_addr);
static void patchAuxv(Elf64_auxv_t *av, unsigned long phnum,
                      unsigned long phdr, unsigned long entry);
off_t get_symbol_offset_with_debug(const char *elf_interpreter, const char *symbol);
off_t get_symbol_offset(const char *pathame, const char *symbol);

typedef void (*PostRestartFnPtr_t)(double, int);

void *ld_so_entry;
LowerHalfInfo_t lh_info_obj;
LowerHalfInfo_t *lh_info;

#define MTCP_RESTART_BINARY "mtcp_restart"

int main(int argc, char *argv[], char *envp[]) {
  set_addr_no_randomize(argv);
  Elf64_Addr cmd_entry;
  int cmd_argc = 0;
  char **cmd_argv;
  void * ld_so_addr = NULL;
  Elf64_Ehdr ld_so_elf_hdr;
  char elf_interpreter[MAX_ELF_INTERP_SZ];
  lh_info = &lh_info_obj;

  memset(lh_info, 0, sizeof(LowerHalfInfo_t));
  unsigned long fsaddr = 0;
  syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
  // Fill in lh_info contents
  lh_info->fsaddr = (void*)fsaddr;
  lh_info->fsgsbase_enabled = CheckAndEnableFsGsBase();

  char *heap_addr = allocate_rtld_heap();

  // Add a guard page before the start of heap; this protects
  // the heap from getting merged with a "previous" region.
  mprotect(heap_addr, HEAP_SIZE, PROT_NONE);

  // Initialize MPI in advance
  int rank;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  
  // Initialize MPI Functions and Constants mapping table in lower-half
  initialize_lh_info();

  // Check arguments and setup arguments for the loader program (cmd)
  // if has "--restore", pass all arguments to mtcp_restart
  int restore_mode = parse_restore_flag(&argc, argv);
  write_lh_info_addr(restore_mode);
  
  if (restore_mode) {
    // RESTART MODE:
    DmtcpRestart dmtcpRestart(argc, argv, BINARY_NAME, MTCP_RESTART_BINARY);
    RestoreTarget *t = get_restore_target_info(dmtcpRestart, rank);
    
    DmtcpCkptHeader ckpt_hdr;
    validate_checkpoint_header(t, ckpt_hdr);
    off_t ckpt_file_pos = 0;
    reserve_memory_areas(t, ckpt_file_pos);
    t->initialize();
    release_reserved_memory(t, ckpt_file_pos);
    restore_session_leadership(t);
    restore_memory_data(t, ckpt_hdr);
    /* Everything restored, close file and finish up */
    close(t->fd());

    // IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

    PostRestartFnPtr_t postRestart = (PostRestartFnPtr_t) ckpt_hdr.postRestartAddr;
    postRestart(0, 0);
    // The following line should not be reached.
    assert(0);
  } else {
    // LAUNCH MODE:
    parse_launch_arguments(argc, argv, &cmd_argc, &cmd_argv);
    // set default loader address
    if (ld_so_addr == NULL) {
      ld_so_addr = (void*) 0x80000000;
    }
    // User application name not provided
    if (cmd_argc == 0) {
      print_usage_and_exit(argv[0]);
    }

    //  Prepend LD_LIBRARY_PATH env variable to
    //    * '/PATH_TO_MANA/lib/dmtcp' for libmpistub.so (always)
    //    * '/PATH_TO_MANA/lib/tmp'   for shadow libraries (only when launched with --use-shadowlibs flag)
    update_library_path(argv[0]);
    get_elf_interpreter(cmd_argv[0], &cmd_entry, elf_interpreter);       
    
    char *interp_base_address =
      load_elf_interpreter(elf_interpreter, ld_so_addr, &ld_so_elf_hdr);
    ld_so_entry = interp_base_address  + ld_so_elf_hdr.e_entry;
    
    char *cmd_argv2[MAX_CMD_ARGV2_LENGTH];
    prepend_elf_interpreter_to_path(cmd_argv, cmd_argc, elf_interpreter, cmd_argv2);
    
    // Deep copy the stack and get the auxv pointer
    Elf64_auxv_t *auxv_ptr;
    void *stack_bottom;
    char *dest_stack = setup_upper_half_stack(argc, argv, cmd_argc, cmd_argv2, interp_base_address, &auxv_ptr, &stack_bottom);

    // -------------------------------------------------------------------------
    // FIXME:
    // **************************************************************************
    // *******   elf_hdr.e_entry is pointing to lower-half instead of to ld.so
    // *******   ld_so_entry and interp_base_address + ld_so_elf_hdr.e_entry
    // *******     should be the same.  Eventually, rationalize this.
    // **************************************************************************
    //   AT_PHDR: "The address of the program headers of the executable."
    // elf_hdr.e_phoff is offset from beginning of file.
    patchAuxv(auxv_ptr, ld_so_elf_hdr.e_phnum,
              (unsigned long)(interp_base_address + ld_so_elf_hdr.e_phoff),
              (unsigned long)interp_base_address + ld_so_elf_hdr.e_entry);
    // info->phnum, (uintptr_t)info->phdr, (uintptr_t)info->entryPoint);
  
    // Insert trampolines for mmap and munmap
    off_t mmap_offset =  get_symbol_offset_with_debug(elf_interpreter, "mmap");
    off_t munmap_offset = get_symbol_offset_with_debug(elf_interpreter, "munmap");
    patch_trampoline((void*)(interp_base_address + mmap_offset), (void*)&mmap_wrapper);
    patch_trampoline((void*)(interp_base_address + munmap_offset), (void*)&munmap_wrapper);

    // Then jump to _start, ld_so_entry, in ld.so (or call it
    //   as fnc that never returns).
    //   > ldso_entrypoint = getEntryPoint(ldso);
    //   > // TODO: Clean up all the registers?
    //   > asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
    //   >               : : "g" (newStack) : "memory");
    //   > asm volatile ("jmp *%0" : : "g" (ld_so_entry) : "memory");
#if defined(__i386__) || defined(__x86_64__)
    asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
                  : : "g" (dest_stack) : "memory");
    asm volatile ("jmp *%0" : : "g" (ld_so_entry) : "memory");
#elif defined(__arm__)
# warning "__arm__ not yet implemented."
#elif defined(__aarch64__)
    asm volatile ("mov sp, %0" : : "r" (dest_stack) );
    asm volatile ("mov x8, %0" : : "g" (ld_so_entry) : "memory");
    asm volatile ("br x8");
#elif defined(__riscv)
    asm volatile ("move sp, %0" : : "r" (dest_stack) : );
    asm volatile ("move t0, %0" : : "g" (ld_so_entry) : "memory");
    asm volatile ("jalr  zero, t0");
#else
# error "current architecture not supported"
#endif /* if defined: __i386__ || x86_64 || __aarch64__ || __riscv */
  }
}

/**
 * @brief Disables address space randomization and re-executes the process.
 *
 * This function checks if the environment variable "MANA_FIRST_TIME" is set. If not,
 *  it disables address space randomization by setting the ADDR_NO_RANDOMIZE 
 *  personality flag and then re-executes the process with the modified environment.
 *  If the variable is already set, it resets it to "0". 
 *
 * @param argv  The argument vector to be passed to execvpe() when 
 *              re-executing the process.
 */
void set_addr_no_randomize(char *argv[]) {
  extern char **environ;
  const char *first_time = "MANA_FIRST_TIME";
  char *env = getenv(first_time);
  if (env == NULL) {
    setenv(first_time, "1", 1);
    personality(ADDR_NO_RANDOMIZE);
    execvpe(argv[0], argv, environ);
  } else {
    env[0] = '0';
  }
}

/**
 * @brief Creates a new heap region for use by the runtime linker (RTLD).
 *
 * This function allocates a new memory region to be used as the heap by 
 *  mapping a memory area starting from the current program break (`sbrk(0)`).
 *  The allocated region is initially set to `PROT_NONE` to prevent accidental access.
 *
 * @return  A pointer to the allocated heap region, or exits the program on failure.
 */
char *allocate_rtld_heap()
{
  // Create new heap region to be used by RTLD
  void *lh_brk = sbrk(0);
  //const uint64_t heapSize = HEAP_SIZE;
  char *heap_addr = (char *)mmap(lh_brk, HEAP_SIZE, PROT_NONE,
      MAP_PRIVATE | MAP_ANONYMOUS |
      MAP_NORESERVE ,
      -1, 0);
  if (heap_addr == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region. Error: %s\n",
        strerror(errno));
    exit(1);
  }
  return heap_addr; 
}

/**
 * @brief Initializes the LowerHalfInfo_t structure with 
 *        MPI constants and function pointers.
 *
 * This function sets up the `lh_info` object by assigning function pointers
 *  and various MPI constants. It should be called after MPI has been initialized.
 *
 */
void initialize_lh_info()
{
  lh_info->mmap = (void*)&mmap_wrapper;
  lh_info->munmap = (void*)&munmap_wrapper;
  lh_info->lh_dlsym = (void*)&lh_dlsym;
  lh_info->mmap_list_fptr = (void*)&get_mmapped_list;
  // MPI constants values
  lh_info->MANA_GROUP_NULL = MPI_GROUP_NULL;
  lh_info->MANA_COMM_NULL = MPI_COMM_NULL;
  lh_info->MANA_REQUEST_NULL = MPI_REQUEST_NULL;
  lh_info->MANA_MESSAGE_NULL = MPI_MESSAGE_NULL;
  lh_info->MANA_OP_NULL = MPI_OP_NULL;
  lh_info->MANA_ERRHANDLER_NULL = MPI_ERRHANDLER_NULL;
  lh_info->MANA_INFO_NULL = MPI_INFO_NULL;
  lh_info->MANA_WIN_NULL = MPI_WIN_NULL;
  lh_info->MANA_FILE_NULL = MPI_FILE_NULL;
  lh_info->MANA_INFO_ENV = MPI_INFO_ENV;
  lh_info->MANA_COMM_WORLD = MPI_COMM_WORLD;
  lh_info->MANA_COMM_SELF = MPI_COMM_SELF;
  lh_info->MANA_GROUP_EMPTY = MPI_GROUP_EMPTY;
  lh_info->MANA_MESSAGE_NO_PROC = MPI_MESSAGE_NO_PROC;
  lh_info->MANA_MAX = MPI_MAX;
  lh_info->MANA_MIN = MPI_MIN;
  lh_info->MANA_SUM = MPI_SUM;
  lh_info->MANA_PROD = MPI_PROD;
  lh_info->MANA_LAND = MPI_LAND;
  lh_info->MANA_BAND = MPI_BAND;
  lh_info->MANA_LOR = MPI_LOR;
  lh_info->MANA_BOR = MPI_BOR;
  lh_info->MANA_LXOR = MPI_LXOR;
  lh_info->MANA_BXOR = MPI_BXOR;
  lh_info->MANA_MAXLOC = MPI_MAXLOC;
  lh_info->MANA_MINLOC = MPI_MINLOC;
  lh_info->MANA_REPLACE = MPI_REPLACE;
  lh_info->MANA_NO_OP = MPI_NO_OP;
  lh_info->MANA_DATATYPE_NULL = MPI_DATATYPE_NULL;
  lh_info->MANA_BYTE = MPI_BYTE;
  lh_info->MANA_PACKED = MPI_PACKED;
  lh_info->MANA_CHAR = MPI_CHAR;
  lh_info->MANA_SHORT = MPI_SHORT;
  lh_info->MANA_INT = MPI_INT;
  lh_info->MANA_LONG = MPI_LONG;
  lh_info->MANA_FLOAT = MPI_FLOAT;
  lh_info->MANA_DOUBLE = MPI_DOUBLE;
  lh_info->MANA_LONG_DOUBLE = MPI_LONG_DOUBLE;
  lh_info->MANA_UNSIGNED_CHAR = MPI_UNSIGNED_CHAR;
  lh_info->MANA_SIGNED_CHAR = MPI_SIGNED_CHAR;
  lh_info->MANA_UNSIGNED_SHORT = MPI_UNSIGNED_SHORT;
  lh_info->MANA_UNSIGNED_LONG = MPI_UNSIGNED_LONG;
  lh_info->MANA_UNSIGNED = MPI_UNSIGNED;
  lh_info->MANA_FLOAT_INT = MPI_FLOAT_INT;
  lh_info->MANA_DOUBLE_INT = MPI_DOUBLE_INT;
  lh_info->MANA_LONG_DOUBLE_INT = MPI_LONG_DOUBLE_INT;
  lh_info->MANA_LONG_INT = MPI_LONG_INT;
  lh_info->MANA_SHORT_INT = MPI_SHORT_INT;
  lh_info->MANA_2INT = MPI_2INT;
  lh_info->MANA_WCHAR = MPI_WCHAR;
  lh_info->MANA_LONG_LONG_INT = MPI_LONG_LONG_INT;
  lh_info->MANA_LONG_LONG = MPI_LONG_LONG;
  lh_info->MANA_UNSIGNED_LONG_LONG = MPI_UNSIGNED_LONG_LONG;
#if 0
  lh_info->MANA_2COMPLEX = MPI_2COMPLEX;
  lh_info->MANA_CXX_COMPLEX = MPI_CXX_COMPLEX;
  lh_info->MANA_2DOUBLE_COMPLEX = MPI_2DOUBLE_COMPLEX;
#endif
  lh_info->MANA_CHARACTER = MPI_CHARACTER;
  lh_info->MANA_LOGICAL = MPI_LOGICAL;
#if 0
  lh_info->MANA_LOGICAL1 = MPI_LOGICAL1;
  lh_info->MANA_LOGICAL2 = MPI_LOGICAL2;
  lh_info->MANA_LOGICAL4 = MPI_LOGICAL4;
  lh_info->MANA_LOGICAL8 = MPI_LOGICAL8;
#endif
  lh_info->MANA_INTEGER = MPI_INTEGER;
  lh_info->MANA_INTEGER1 = MPI_INTEGER1;
  lh_info->MANA_INTEGER2 = MPI_INTEGER2;
  lh_info->MANA_INTEGER4 = MPI_INTEGER4;
  lh_info->MANA_INTEGER8 = MPI_INTEGER8;
  lh_info->MANA_REAL = MPI_REAL;
  lh_info->MANA_REAL4 = MPI_REAL4;
  lh_info->MANA_REAL8 = MPI_REAL8;
  lh_info->MANA_REAL16 = MPI_REAL16;
  lh_info->MANA_DOUBLE_PRECISION = MPI_DOUBLE_PRECISION;
  lh_info->MANA_COMPLEX = MPI_COMPLEX;
  lh_info->MANA_COMPLEX8 = MPI_COMPLEX8;
  lh_info->MANA_COMPLEX16 = MPI_COMPLEX16;
  lh_info->MANA_COMPLEX32 = MPI_COMPLEX32;
  lh_info->MANA_DOUBLE_COMPLEX = MPI_DOUBLE_COMPLEX;
  lh_info->MANA_2REAL = MPI_2REAL;
  lh_info->MANA_2DOUBLE_PRECISION = MPI_2DOUBLE_PRECISION;
  lh_info->MANA_2INTEGER = MPI_2INTEGER;
  lh_info->MANA_INT8_T = MPI_INT8_T;
  lh_info->MANA_UINT8_T = MPI_UINT8_T;
  lh_info->MANA_INT16_T = MPI_INT16_T;
  lh_info->MANA_UINT16_T = MPI_UINT16_T;
  lh_info->MANA_INT32_T = MPI_INT32_T;
  lh_info->MANA_UINT32_T = MPI_UINT32_T;
  lh_info->MANA_INT64_T = MPI_INT64_T;
  lh_info->MANA_UINT64_T = MPI_UINT64_T;
  lh_info->MANA_AINT = MPI_AINT;
  lh_info->MANA_OFFSET = MPI_OFFSET;
  lh_info->MANA_C_BOOL = MPI_C_BOOL;
  lh_info->MANA_C_COMPLEX = MPI_C_COMPLEX;
  lh_info->MANA_C_FLOAT_COMPLEX = MPI_C_FLOAT_COMPLEX;
  lh_info->MANA_C_DOUBLE_COMPLEX = MPI_C_DOUBLE_COMPLEX;
  lh_info->MANA_C_LONG_DOUBLE_COMPLEX = MPI_C_LONG_DOUBLE_COMPLEX;
  lh_info->MANA_CXX_BOOL = MPI_CXX_BOOL;
  lh_info->MANA_CXX_FLOAT_COMPLEX = MPI_CXX_FLOAT_COMPLEX;
  lh_info->MANA_CXX_DOUBLE_COMPLEX = MPI_CXX_DOUBLE_COMPLEX;
  lh_info->MANA_CXX_LONG_DOUBLE_COMPLEX = MPI_CXX_LONG_DOUBLE_COMPLEX;
  lh_info->MANA_COUNT = MPI_COUNT;
  lh_info->MANA_ERRORS_ARE_FATAL = MPI_ERRORS_ARE_FATAL;
  lh_info->MANA_ERRORS_RETURN = MPI_ERRORS_RETURN;
}

/*
 * @brief Writes the address of lh_info to a persistent location.
 *
 * This function ensures that the address of `lh_info` is stored 
 *  in a way that prevents infinite loops during launch and 
 *  accidental address changes upon restart. 
 * Restore Mode: 
 *  write the address to a temporary file in `/tmp` using a 
 *  file name formatted `mana_tmp_lh_info_[hostname]_[pid]`.  
 * Launch Mode:
 *  write the address to an environment variable.  
 * 
 * If any file operation fails, the function logs an error and exits.
 *
 * @param restore_mode Non-zero value indicate restore mode, 
 *                      else launch mode
 */
void write_lh_info_addr(int restore_mode) 
{
    if (restore_mode) {
    // File name format: mana_tmp_lh_info_[hostname]_[pid]
    char filename[100] = "/tmp/mana_tmp_lh_info_";
    gethostname(filename + strlen(filename), 100 - strlen(filename));
    snprintf(filename + strlen(filename), 100 - strlen(filename), "_%d", getpid());
    struct stat statbuf;
    if (stat(filename, &statbuf) == 0) {
      unlink(filename);
      int unlink_errno;
      if (stat(filename, &statbuf) == 0) {
        DLOG(ERROR, "Could not remove pre-existing file %s. Error: %s", filename, strerror(unlink_errno));
        exit(1);
      }
    }
    int fd = open(filename, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
      DLOG(ERROR, "Could not create addr.bin file. Error: %s", strerror(errno));
      exit(1);
    }
    ssize_t rc = write(fd, &lh_info, sizeof(lh_info));
    if (rc < static_cast<ssize_t>(sizeof(lh_info))) {
      DLOG(ERROR, "Wrote fewer bytes than expected to addr.bin. Error: %s",
          strerror(errno));
      exit(1);
    }
    close(fd);
  } else {
    char lh_info_addr_str[20];
    snprintf(lh_info_addr_str, 20, "%p", lh_info);
    setenv("MANA_LH_INFO_ADDR", lh_info_addr_str, 1);
  }
}


// -------------------- restart helpers
/**
 * @brief Parses and removes the "--restore" flag from command-line arguments.
 *
 * This function checks if the "--restore" flag is present in the command-line
 *  arguments. If found, it removes the flag from the argument list and sets
 *  the restore mode to `1`. This is required because DMTCP does not recognize 
 *  the flag, but the restart logic need to handle it.
 *
 * @param argc  Pointer to the number of command-line arguments.
 * @param argv  Array of command-line arguments.
 * @return      1 if "--restore" flag is found and removed, 0 otherwise.
 */
int parse_restore_flag(int *argc, char **argv)
{
  int restore_mode = 0;
  // Iterate over command line arguments
  for (int i = 1; i < *argc; i++) {
    if (strcmp(argv[i], "--restore") == 0) {
      restore_mode = 1;
      // Remove the --restore flag because dmtcp does not recognize
      // this flag.
      (*argc)--;
      for (int j = i; j < *argc; j++) {
        argv[j] = argv[j + 1];
      }
      break;
    }
  }
  return restore_mode;
}

/**
 * @brief Retrieves the restore target based checkpoint image location.
 *
 * This function determines the correct checkpoint image to restore from.
 *  If a restart directory is specified, it constructs the correct image path
 *  based on the rank of the process.
 *
 * @param dmtcpRestart  Reference to the DmtcpRestart object holding restart info.
 * @param rank          Rank of the process in MPI-based applications.
 * @return              A pointer to the a RestoreTarget object representing the 
 *                      checkpoint image to restore application from.
 */
RestoreTarget* get_restore_target_info(DmtcpRestart &dmtcpRestart, int rank)
{
  RestoreTarget *t;
  // if no resatrt direcory present 
  if (dmtcpRestart.restartDir.empty()) {
     t = new RestoreTarget(dmtcpRestart.ckptImages[0]);
   } else{
     // read from directory '/ckpt_rank_X/', where X=process rank 
     string image_path;
     string restart_dir = dmtcpRestart.restartDir;
     string image_dir = restart_dir + "/ckpt_rank_" + jalib::XToString(rank) + "/";
     vector<string> files = jalib::Filesystem::ListDirEntries(image_dir);
     for (const string &file : files) {
        if (Util::strStartsWith(file.c_str(), "ckpt") &&
            Util::strEndsWith(file.c_str(), ".dmtcp")) {
          image_path = image_dir + file;
          break;
        }
     }
    t = new RestoreTarget(image_path.c_str());
   }
  JASSERT(t->pid() != 0);
  return t;
}

/**
 * @brief Validate the checkpoint file header
 *
 * Reads and verifies the checkpoint file header to ensure it matches
 *  the expected MTCP (Multi-Threaded Checkpointing) signature.
 *
 * @param t         Pointer to the RestoreTarget object representing the checkpoint.
 * @param ckpt_hdr  Reference to the DmtcpCkptHeader struct that stores valid header.
 */
void validate_checkpoint_header(RestoreTarget *t, DmtcpCkptHeader &ckpt_hdr)
{
  ssize_t rc = read(t->fd(), &ckpt_hdr, sizeof(ckpt_hdr));
  ASSERT_EQ(rc, static_cast<ssize_t>(sizeof(ckpt_hdr)));
  ASSERT_EQ(string(ckpt_hdr.ckptSignature), string(DMTCP_CKPT_SIGNATURE));
}

/**
 * @brief Reserve memory regiions for process restoration.
 *
 * Iterates over the checkpoint file and reserves memory regions without
 *  replacement to ensure a consistent memory layout for restoration.
 *
 * @param t             Pointer to the RestoreTarget object representing the checkpoint.
 * @param ckpt_file_pos Reference to the file position within the checkpoint image.
 */
void reserve_memory_areas(RestoreTarget *t, off_t &ckpt_file_pos)
{
  ProcMapsArea area;
  ckpt_file_pos = lseek(t->fd(), 0, SEEK_CUR);
  while(1) {
    ssize_t rc = read(t->fd(), &area, sizeof area);
    assert(rc == sizeof area);
    if (area.addr == NULL) {
      lseek(t->fd(), ckpt_file_pos, SEEK_SET);
      break;
    }
    if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
      void *mmapedat = mmap_fixed_noreplace(area.addr, area.size, PROT_NONE,
                                            MAP_PRIVATE | MAP_ANONYMOUS,
                                            0, 0);
      if (mmapedat == MAP_FAILED) {
        fprintf(stderr, "restore failed area.addr: %p, area.endAddr%p\n", area.addr, area.endAddr);
        volatile int dummy = 1;
        while (dummy);
      }
    }
    if ((area.properties & DMTCP_ZERO_PAGE) == 0 &&
        (area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {
      off_t seekLen = area.size;
      if (!(area.flags & MAP_ANONYMOUS) && area.mmapFileSize > 0) {
        seekLen =  area.mmapFileSize;
      }
      lseek(t->fd(), seekLen, SEEK_CUR);
    }
  }
}

/**
 * @brief Releases reserved memory areas before restoring process memory.
 *
 * This function iterates over the reserved memory regions and unmaps 
 *  them before restoring the actual memory data.
 * 
 * @param t             Pointer to the RestoreTarget object representing the checkpoint.
 * @param ckpt_file_pos Reference to the file position within the checkpoint image.
 */
void release_reserved_memory(RestoreTarget *t, off_t ckpt_file_pos)
{
  ProcMapsArea area;
  while(1) {
    ssize_t rc = read(t->fd(), &area, sizeof area);
    assert(rc > 0);
    if (area.addr == NULL) {
      lseek(t->fd(), ckpt_file_pos, SEEK_SET);
      break;
    }
    if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
      munmap(area.addr, area.size);
    }
    if ((area.properties & DMTCP_ZERO_PAGE) == 0 &&
        (area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {
      off_t seekLen = area.size;
      if (!(area.flags & MAP_ANONYMOUS) && area.mmapFileSize > 0) {
        seekLen =  area.mmapFileSize;
      }
      lseek(t->fd(), seekLen, SEEK_CUR);
    }
  }
}

/**
 * @brief Restores session leadership if necessary.
 *
 * If the process was a session leader before checkpointing, this function
 *  ensures that it regains session leadership upon restart.
 *
 * @param t             Pointer to the RestoreTarget object representing the checkpoint.
 */
void restore_session_leadership(RestoreTarget *t)
{
  if (t->sid() == t->pid()) {
    if (getsid(0) != t->pid()) {
      JWARNING(setsid() != -1)
        (getsid(0))(JASSERT_ERRNO)
        .Text("Failed to resotre this process as session leader.");
    }
  }
}

/**
 * @brief Restores all memory regions from the checkpoint image.
 *
 * Restore all the memory regions from the checkpoint file
 *  by reading and mapping them back to the memory.
 *
 * @param t             Pointer to the RestoreTarget object representing the checkpoint.
 * @param ckpt_file_pos Reference to the file position within the checkpoint image.
 */
void restore_memory_data(RestoreTarget *t, DmtcpCkptHeader &ckpt_hdr)
{
  while (1) {
    int ret = restoreMemoryArea(t->fd(), &ckpt_hdr);
    if (ret == -1) {
      break; /* end of ckpt image */
    }
  }
}

/**
 * @brief Restores a memory area from a checkpoint file descriptor.
 *
 * This function is responsible for restoring a memory region that was previous
 *  saved in a checkpoint file. It handles different types of memory areas, 
 *  including stack, heap, and anonymous regions, and ensures proper flags, 
 *  protections, and mappings are set when restoring the memory region.
 *
 * The function uses memory-mapping (mmap) to restore the region and adjusts 
 *  properties like protection flags, whether the region should be private or 
 *  anonymous, and whether it should be marked as zero-paged.
 *
 * FIXME: This function is copied from DMTCP's mtcp_restart.c
 *  For maintenance, we should stop having two copies of the 
 *  same function. A possible plan is using "ld.so" and "mtcp_restart"
 *  to restore the upper-half instead of using this lower-half program
 *
 * @param fd      The file descriptor of the checkpoint file 
 *                containing the memory data.
 * @param ckptHdr The checkpoint header containing metadata 
 *                about the memory regions.
 * @return        0 if sucessful, -1 if the restoration fails.
 */
static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr)
{
  int imagefd;
  void *mmappedat;

  /* Read header of memory area into area; mtcp_readfile() will read header */
  ProcMapsArea area;

  int rc = read(fd, &area, sizeof area);
  assert(rc == sizeof area);
  if (area.addr == NULL) {
    return -1;
  }

  if (area.name[0] && strstr(area.name, "[heap]")
      && (VA)sbrk(0) != area.addr + area.size) {
    // JWARNING(false);
    // ("WARNING: break (%p) not equal to end of heap (%p)\n", mtcp_sys_brk(NULL), area.addr + area.size);
  }

  /* MAP_GROWSDOWN flag is required for stack region on restart to make
   * stack grow automatically when application touches any address within the
   * guard page region(usually, one page less then stack's start address).
   *
   * The end of stack is detected dynamically at checkpoint time. See
   * prepareDmtcpCkptHeader() in threadlist.cpp and ProcessInfo::growStack()
   * in processinfo.cpp.
   */
  if ((area.name[0] && area.name[0] != '/' && strstr(area.name, "stack"))
      || (area.endAddr == (VA) ckptHdr->endOfStack)) {
    area.flags = area.flags | MAP_GROWSDOWN;
    JTRACE("Detected stack area")(ckptHdr->endOfStack) (area.endAddr);
  }

  // We could have replaced MAP_SHARED with MAP_PRIVATE in writeckpt.cpp
  // instead of here. But we do it this way for debugging purposes. This way,
  // readdmtcp.sh will still be able to properly list the shared memory areas.
  if (area.flags & MAP_SHARED) {
    area.flags = area.flags ^ MAP_SHARED;
    area.flags = area.flags | MAP_PRIVATE | MAP_ANONYMOUS;
  }

  /* Now mmap the data of the area into memory. */

  /* CASE MAPPED AS ZERO PAGE: */
  if ((area.properties & DMTCP_ZERO_PAGE) != 0) {
    JTRACE("restoring zero-paged anonymous area, %p bytes at %p\n") (area.size) (area.addr);
    // No need to mmap since the region has already been mmapped by the parent
    // header.
    // Just restore write-protection if needed.
    if (!(area.prot & PROT_WRITE)) {
      JASSERT(mprotect(area.addr, area.size, area.prot) == 0);
    }
  }

  /* CASE MAP_ANONYMOUS (usually implies MAP_PRIVATE):
   * For anonymous areas, the checkpoint file contains the memory contents
   * directly.  So mmap an anonymous area and read the file into it.
   * If file exists, turn off MAP_ANONYMOUS: standard private map
   */
  else {
    /* If there is a filename there, though, pretend like we're mapping
     * to it so a new /proc/self/maps will show a filename there like with
     * original process.  We only need read-only access because we don't
     * want to ever write the file.
     */

    if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
      // MMAP only if it's not a child header.
      imagefd = -1;
      if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
        imagefd = open(area.name, O_RDONLY, 0);
        if (imagefd >= 0) {
          /* If the current file size is smaller than the original, we map
           * the region as private anonymous. Note that with this we lose
           * the name of the region but most applications may not care.
           */
          off_t curr_size = lseek(imagefd, 0, SEEK_END);
          JASSERT(curr_size != -1);
          if ((static_cast<size_t>(curr_size) < area.size + area.offset) &&
              (area.prot & PROT_WRITE)) {
            JTRACE("restoring non-anonymous area %s as anonymous: %p bytes at %p\n")
                  (area.name) (area.size) (area.addr);
            close(imagefd);
            imagefd = -1;
            area.offset = 0;
            area.flags |= MAP_ANONYMOUS;
          }
        }
      }

      if (area.flags & MAP_ANONYMOUS) {
        JTRACE("restoring anonymous area, %p bytes at %p\n")
              (area.size) (area.addr);
      } else {
        JTRACE("restoring to non-anonymous area,"
                " %p bytes at %p from %s + 0x%X\n") (area.size) (area.addr) (area.name) (area.offset);
      }

      /* Create the memory area */

      // If the region is marked as private but without a backing file (i.e.,
      // the file was deleted on ckpt), restore it as MAP_ANONYMOUS.
      // TODO: handle huge pages by detecting and passing MAP_HUGETLB in flags.
      if (imagefd == -1 && (area.flags & MAP_PRIVATE)) {
        area.flags |= MAP_ANONYMOUS;
      }

      /* POSIX says mmap would unmap old memory.  Munmap never fails if args
      * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
      * mtcp_safemmap here to check for address conflicts.
      */
      mmappedat = restore_mmap(area.addr, area.size, area.prot | PROT_WRITE,
                               area.flags, imagefd, area.offset);

      if (mmappedat != area.addr) {
        fprintf(stderr, "restore failed area.addr: %p, area.endAddr%p\n", area.addr, area.endAddr);
        volatile int dummy;
        while (dummy);
      }
      JASSERT(mmappedat == area.addr);

      /* Close image file (fd only gets in the way) */
      if (imagefd >= 0) {
        close(imagefd);
      }
    }

    if ((area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {
      // Parent header doesn't have any follow on data.

      /* This mmapfile after prev. mmap is okay; use same args again.
       *  Posix says prev. map will be munmapped.
       */

      /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
      if (area.mmapFileSize > 0 && area.name[0] == '/') {
        JTRACE("restoring memory region %p of %p bytes at %p\n") (area.mmapFileSize) (area.size) (area.addr);
        ssize_t rc = 0;
        int count = 0;
        do {
          rc = read(fd, area.addr + count, area.mmapFileSize - count);
          count += rc;
        } while (rc > 0 && count < area.mmapFileSize);
        JASSERT(count == area.mmapFileSize);
      } else {
        size_t rc = 0, count = 0;
        do {
          rc = read(fd, area.addr + count, area.size - count);
          count += rc;
        } while (rc > 0 && count < area.size);
        if (count != area.size) {
          volatile int dummy = 1;
          while (dummy);
        }
        JASSERT(count == area.size);
      }

      if (!(area.prot & PROT_WRITE)) {
        JASSERT(mprotect(area.addr, area.size, area.prot) == 0);
      }
    }
  }
  return 0;
}

/**
 * @brief Parses command-line arguments for the user application.
 * Extracts the command-line arguments meant for the upper half (user application)
 *  by processing flags such as `-h` and determining where the loader program's 
 *  arguments begin.
 *
 * @param argc      Number of command-line arguments.
 * @param argv      Array of command-line arguments.
 * @param cmd_argc  Pointer to store the number of arguments for the user application.
 * @param cmd_argv  Pointer to store the argument array for the user application.
 */
void parse_launch_arguments(int argc, char **argv, int *cmd_argc, char ***cmd_argv)
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
     print_usage_and_exit(argv[0]); 
    } else {
      // Break at the first argument of the loader program (the program name)
      *cmd_argc = argc - i;
      *cmd_argv = argv + i;
      break;
    }
  }
}

/**
 * @brief Prints usage instructions and exits the program.
 *
 * Displays a message explaining the correct usage of the program and 
 * then exits with an error code.
 *
 * @param prog_name  Name of the executable.
 */
void print_usage_and_exit(char *prog_name)
{
  fprintf(stderr, "USAGE:  %s [-a load_address] <command_line>\n", prog_name);
  fprintf(stderr, "  (load_address is typically a multiple of 0x200000)\n");
  exit(1);
}

/**
 * @brief Updates the LD_LIBRARY_PATH for the upper-half application.
 *
 * Update LD_LIBRARY_PATH env variable for the Upper-Half(user's MPI application). 
 *  The env variable will be prepended with:
 *  a) location of libmpistub.so library, that contains 
 *      MPI-symbols required for user's MPI application.
 *  b) location of 'shadow-libraries', that cintain sym-link 
 *      for dynamic libraries with constructors.
 *
 * NOTE:  This is necessary when an application is compiled with `mpicc_mana`
 *        instead of `mpicc`, or when shadow libraries are needed for dynamic linking.
 *
 * @param argv0 the path to the executing binary. 
 */
void update_library_path(const char *argv0)
{
  const char *remove_part = "bin/../bin/lower-half";
  size_t base_len = strlen(argv0) - strlen(remove_part);

  // constructing lib1 path: "/path_to_mana/lib/dmtcp"
  const char *lib1_suffix = "lib/dmtcp";
  size_t lib1_len = base_len + strlen(lib1_suffix) + 1;
  char *lib1 = static_cast<char*>(malloc(lib1_len));
  if (!lib1) {
    perror("Malloc for lib1 path failed");
    exit(1);
  }
  strncpy(lib1, argv0, base_len);
  lib1[base_len] = '\0';
  strcat(lib1, lib1_suffix);

  // prepend lib1 to LD_LIBRARY_PATH
  if (prepend_to_library_path(lib1) != 0) {
    perror("Lib1 prepend to LD_LIBRARY_PATH failed");
    exit(1);
  }
  free(lib1); 

  // constructing lib2 path: "/path_to_mana/lib/tmp"
  const char *lib2_suffix = "lib/tmp";
  size_t lib2_len = base_len + strlen(lib2_suffix) + 1;
  char *lib2 = static_cast<char*>(malloc(lib2_len));
  if (!lib2) {
    perror("Malloc for lib2 path failed");
    exit(1);
  }
  strncpy(lib1, argv0, base_len);
  lib2[base_len] = '\0';
  strcat(lib2, lib2_suffix);

  // checking if tmp directory for shadow-libs exist
  struct stat info;
  if (stat(lib2, &info) == 0) {
    // prepend lib2 to LD_LIBRARY_PATH
    if(prepend_to_library_path(lib2) != 0) {
      perror("");
      exit(1);
    }
  }
  free(lib2);
}

/**
 * @brief Prepends a new library path to LD_LIBRARY_PATH.
 *
 * This function modifies the `LD_LIBRARY_PATH` environment variable by 
 *  adding the specified `new_path` at the beginning, ensuring that libraries
 *  in this path are searched first. If `LD_LIBRARY_PATH` was previously set,
 *  the new path is prepended to it with a colon (`:`) separator.
 *
 * @param new_path  The new library path to add.
 * @return          0 on sucess, -1 on memory allocation failure.
 */
int prepend_to_library_path(const char *new_path) {
  const char *old_path = getenv("LD_LIBRARY_PATH");
  size_t new_len = strlen(new_path) + strlen(old_path) + 2;

  char *updated_path = static_cast<char*>(malloc(new_len));
  if (!updated_path) {
    return -1;
  }
  if (old_path) {
    snprintf(updated_path, new_len, "%s:%s", new_path, old_path);
  } else {
    snprintf(updated_path, new_len, "%s", new_path);
  }

  setenv("LD_LIBRARY_PATH", updated_path, 1);
  free(updated_path);
  return 0;
}


/** 
 *
 * @brief Opens the ELF executable to extract ELF interpreter and entry point from an ELF binary.
 *
 * This function reads the ELF header and program headers of the given file descriptor 
 *  to retrieve the entry point of the executable and the path to its interpreter (e.g., ld.so). 
 *  It ensures the file is a valid 64-bit ELF binary and locates the PT_INTERP segment.
 *
 * @param cmd_path        Path to executable file.
 * @param cmd_entry       Pointer to store the entry point of the ELF binary.
 * @param elf_interpreter Buffer to store the path of the ELF interpreter (ld.so)
 */
void get_elf_interpreter(const char *cmd_path, Elf64_Addr *cmd_entry,
                         char elf_interpreter[]) 
{ 
  unsigned char e_ident[EI_NIDENT];
  int fd = open(cmd_path, O_RDONLY);
  if (fd < 0) {
    perror("Failed to open ELF executable");
    exit(1);
  }

  ssize_t rc = read(fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp((char *)e_ident, ELFMAG, strlen(ELFMAG)) == 0);
  // FIXME:  Add support for 32-bit ELF later
  assert(e_ident[EI_CLASS] == ELFCLASS64);

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));
  *cmd_entry = elf_hdr.e_entry;

  // Find ELF interpreter
  int phoff = elf_hdr.e_phoff;
  lseek(fd, phoff, SEEK_SET);
  Elf64_Phdr phdr;
  int i;
  for (i = 0; ; i++) {
    assert(i < elf_hdr.e_phnum);
    rc = read(fd, &phdr, sizeof(phdr)); // Read consecutive program headers
    if (phdr.p_type == PT_INTERP) break;
  }
  lseek(fd, phdr.p_offset, SEEK_SET); // Point to beginning of elf interpreter
  assert(phdr.p_filesz < MAX_ELF_INTERP_SZ);
  rc = read(fd, elf_interpreter, phdr.p_filesz);
  assert(rc == static_cast<ssize_t>(phdr.p_filesz));
  close(fd);
}

/**
 * @brief Loads the ELF interpreter into memory.
 *
 * This function opens the specified ELF interpreter (e.g., dynamic linker `ld.so`),
 *  verifies that it is a valid 64-bit ELF binary, and maps its loadable segments
 *  into memory at the specified address. It also extracts the ELF header 
 *  information for further use.
 *
 * @param elf_interpreter   Path to the ELF interpreter binary (e.g., "/lib64/ld-linux-x86-64.so.2").
 * @param ld_so_addr        Address where the ELF inetrpreter should be loaded.
 * @param ld_so_elf_hdr_ptr Pointer to an `ELF64_Ehdr` struct to store the ELF header.
 * @return                  A pointer to the base address where the ELF interpreter is loaded.
 */
char *load_elf_interpreter(char elf_interpreter[],
                           void *ld_so_addr, Elf64_Ehdr *ld_so_elf_hdr_ptr) 
{
  unsigned char e_ident[EI_NIDENT];
  int fd;
  int rc;

  fd = open(elf_interpreter, O_RDONLY);
  rc = read(fd, e_ident, sizeof(e_ident));
  assert(rc == sizeof(e_ident));
  assert(strncmp((char *)e_ident, ELFMAG, sizeof(ELFMAG)-1) == 0);
  // FIXME:  Add support for 32-bit ELF later
  assert(e_ident[EI_CLASS] == ELFCLASS64);

  // Reset fd to beginning and parse file header
  lseek(fd, 0, SEEK_SET);
  Elf64_Ehdr elf_hdr;
  rc = read(fd, &elf_hdr, sizeof(elf_hdr));
  assert(rc == sizeof(elf_hdr));

  // Find ELF interpreter
  int phoff = elf_hdr.e_phoff;
  Elf64_Phdr phdr;
  int i;
  char *interp_base_address = NULL;
  lseek(fd, phoff, SEEK_SET);
  for (i = 0; i < elf_hdr.e_phnum; i++ ) {
    rc = read(fd, &phdr, sizeof(phdr)); // Read consecutive program headers
    if (phdr.p_type == PT_LOAD) {
      // PT_LOAD is the only type of loadable segment for ld.so
      interp_base_address = map_elf_interpreter_load_segment(fd, phdr,
                                                             ld_so_addr);
    }
  }

  assert(interp_base_address);
  *ld_so_elf_hdr_ptr = elf_hdr;
  close(fd);
  return interp_base_address;
}

/**
 * @brief Maps a loadable segment of the ELF interpreter into memory.
 * 
 * This function maps a `PT_LOAD` segment of the ELF interpreter (e.g., `ld.so`)
 *  into memory, ensuring proper page alignment and protection flags. It handles
 *  the mapping of both the `data` and `bss` sections, zeroing out any uninitialized
 *  memory as required by the ELF format. The first segment determines 
 *  the base address of `ld.so`, while subsequent segments are mapped relative to it.
 *
 * @param fd          File descriptor of the ELF interpreter.
 * @param phdr        ELF program header describing the segment.
 * @param ld_so_addr  Base address where `ld.so` should be loaded 
 *                    (only relevant for first segment).
 * @return            Base address of the mapped ELF interpretter.
 *
 */
char * map_elf_interpreter_load_segment(int fd, Elf64_Phdr phdr,
                                      void *ld_so_addr) {
  static char *base_address = NULL; // is NULL on call to first LOAD segment
  static int first_time = 1;
  int prot = PROT_NONE;
  if (phdr.p_flags & PF_R)
    prot |= PROT_READ;
  if (phdr.p_flags & PF_W)
    prot |= PROT_WRITE;
  if (phdr.p_flags & PF_X)
    prot |= PROT_EXEC;
  assert(phdr.p_memsz >= phdr.p_filesz);
  // NOTE:  man mmap says:
  // For a file that is not a  multiple  of  the  page  size,  the
  // remaining memory is zeroed when mapped, and writes to that region
  // are not written out to the file.
  void *rc2;
  // Check ELF Format constraint:
  if (phdr.p_align > 1) {
    assert(phdr.p_vaddr % phdr.p_align == phdr.p_offset % phdr.p_align);
  }
  // Desired memory layout to be created by mmap():
  //   addr: base_addr + phdr.p_vaddr - PAGE_OFFSET(phdr.p_vaddr)
  //         [ mmap requires that addr be a multiple of pagesize ]
  //   len:  phdr.p_memsz + PAGE_OFFSET(phdr.p_vaddr)
  //        [ Add PAGE_OFFSET(phdr.p_vaddr) to compensate terms in addr/offset ]
  //   offset: phdr.p_offset - PAGE_OFFSET(phdr.p_offset)
  //         [ mmap requires that offset be a multiple of pagesize ]
  // NOTE:
  //   mapped data segment:  FROM: addr
  //                         TO: base_addr + phdr.p_vaddr + phdr.p_memsz
  //   The data segment splits into a data section and a bss section:
  //     data section: FROM:  addr
  //                   TO: base_addr + phdr.p_vaddr + phdr.p_filesz
  //     bss section:  FROM:  base_addr + phdr.p_vaddr + phdr.p_filesz
  //                   TO: base_addr + phdr.p_vaddr + phdr.p_memsz
  //   After mapping in bss section, must zero it out.  We will directly
  //     zero out the bss using memset() after mapping in data+bss.
  //     This works because we use MAP_PRIVATE (and not MAP_SHARED).
  //     'man mmap' says:
  //       "If MAP_PRIVATE is specified, modifications to the mapped data
  //        by the calling process shall be visible only to the calling
  //        process and shall not change the underlying object."
  // NOTE:  base_address is 0 for first load segment of ld.so

  // To satisfy the page-aligned constraints of mmap, an ELF object
  //   should guarantee that the page offsets of p_vaddr and p_offset are equal.
  assert(PAGE_OFFSET(phdr.p_vaddr) == PAGE_OFFSET(phdr.p_offset));

  // NOTE:  We want base_addr + phdr.p_vaddr to correspond to
  //   phdr.p_offset in the backing file.  The above assertions imply that:
  //   ROUND_DOWN(base_addr + phdr.p_vaddr) should correspond to
  //      ROUND_DOWN(phdr.p_offset).
  //   Since base_addr is already a pagesize multiple,
  //     we have:  ROUND_DOWN(base_addr + phdr.p_vaddr) ==
  //               base_addr + ROUND_DOWN(phdr.p_vaddr)
  //   If we choose these as addr and offset in the call to mmap(),
  //     then we must compensate in len, by adding to len:
  //       (phdr.p_vaddr - ROUND_DOWN(phdr.p_vaddr)) [PAGE_OFFSET(phdr.p_vaddr)]
  //   This yields the following parameters for mmap():
  unsigned long addr = (first_time ?
                        (unsigned long)ld_so_addr + ROUND_DOWN(phdr.p_vaddr, PAGE_SIZE) :
                        (unsigned long)base_address + ROUND_DOWN(phdr.p_vaddr, PAGE_SIZE));
  // addr was rounded down, above.  To compensate, we round up here by a
  //   fractional page for phdr.p_vaddr.
  unsigned long len = phdr.p_memsz + PAGE_OFFSET(phdr.p_vaddr);
  unsigned long offset = ROUND_DOWN(phdr.p_offset, PAGE_SIZE);

  // NOTE:
  //   As an optimized alternative, we could map the data segment with two
  //       mmap calls, similarly to what the kernel does.  In that case,
  //       we would not need a separate call to memset() to zero out the bss,
  //       and the kernel would label the bss as anonymous (rather
  //       than label all of data+bss with the ld.so file).
  //     The first call maps in the data section using a len of phdr.p_filesz
  //       mmap will map in the remaining fraction of a page and make the
  //       fractional page act as MAP_ANONYMOUS (zeroed out)
  //     The second call maps in the remaining part of the bss section,
  //       using a len of phdr.p_memsz - phdr.p_filesz
  //       This is done with MAP_ANONYMOUS so that mmap will zero it out.

  // FIXME:  On first load segment, we should map 0x400000 (2*phdr.p_align),
  //         and then unmap the unused portions later after all the
  //         LOAD segments are mapped.  This is what ld.so would do.
  if (first_time && ld_so_addr == NULL) {
    rc2 = mmap_wrapper((void *)addr, len, prot, MAP_PRIVATE, fd, offset);
  } else {
    assert((char *)addr+len >=
           (char *)base_address + phdr.p_vaddr + phdr.p_memsz);
    rc2 = mmap_wrapper((void *)addr, len, prot, MAP_PRIVATE|MAP_FIXED, fd, offset);
  }
  if (rc2 == MAP_FAILED) {
    perror("mmap"); exit(1);
  }
  // Required by ELF Format:
  //   memory between rc2+phdr.p_filesz and rc2+phdr.p_memsz must be zeroed out.
  //   The second LOAD segment of ld.so is a data segment with
  //   phdr.p_memsiz > phdr.p_filesz, and this difference is the bss section.
  // Required by 'man mmap':
  //   For a mapping from addr to addr+len, if this is not a  multiple
  //   of  the  page  size,  then the fractional remaining portion of
  //   the memory i ge s zeroed when mapped, and writes to that region
  //   are not written out to the file.
  if (phdr.p_memsz > phdr.p_filesz) {
    assert((char *)addr+len == base_address + phdr.p_vaddr + phdr.p_memsz);
    // Everything up to phdr.p_filesz is the data section until the bss.
    // The memory to be zeroed out, below, corresponds to the bss section.
    memset((char *)base_address + phdr.p_vaddr + phdr.p_filesz, '\0',
           phdr.p_memsz - phdr.p_filesz);

    // Next, we zero out the memory beyond the bss section that is still
    // on the same memory page.  This is needed due to a design bug
    // in glibc ld.so as of glibc-2.27.  The Linux kernel implementation
    // of the ELF loader uses two mmap calls for loading the data segment,
    // instead of our own strategy of one mmap call and one memset as above.
    // The second mmap call by the kernel uses MAP_ANONYMOUS, which has
    // the side effect of zeroing out all memory on any page containing part
    // of the bss.  The glibc ld.so code chooses to depend directly on this
    // accident of kernel implementation that zeroes out additional memory,
    // even though this goes beyond the specs in the ELF manual.
    char *endBSS = (char *)base_address + phdr.p_vaddr + phdr.p_memsz;
    memset(endBSS, '\0', (char *)ROUND_UP(endBSS, PAGE_SIZE) - endBSS);
  }

  if (first_time) {
    first_time = 0;
    base_address = (char*) rc2;
  }
  return base_address;
}

/**
 * @brief Prepares the argument list by prepending the ELF interpreter.
 *
 * This function constructs a new argument list where the ELF interpreter (ld.so)
 *  is placed before the user application's arguments.
 *
 * @param cmd_argv        Original argument list of User-application.
 * @param cmd_argc        Number of original arguments.
 * @param elf_interpreter Path to ELF interpreter (ld.so).
 * @param cmd_argv2       Output array of stored modified argument list.
 *
 */
void prepend_elf_interpreter_to_path(char **cmd_argv, int cmd_argc, char elf_interpreter[], char *cmd_argv2[])
{
  int i;
  assert(cmd_argc + 1 < MAX_CMD_ARGV2_LENGTH);
  // Prepend the ELF interpreter
  cmd_argv2[0] = strdup(elf_interpreter); 
  // Copy exsisting arguments
  for (i = 0; i < cmd_argc; i++) {
    cmd_argv2[i+1] = cmd_argv[i];
  }
  cmd_argv2[i+1] = NULL;
}

/**
 * @brief Sets up the stack by deep copying it and defining auxiliary values.
 *
 * This function ensures a proper stack setup for user-application in 
 *  the upper half process (refer: split processes architecture), calculate 
 *  stack limits, and applies necessary memory protections.
 *
 * @param argc                Number of arguments.
 * @param argv                Argument list.
 * @param cmd_argc            Modified argument count.
 * @param cmd_argv2           Modified argument list with ELF interpreter.
 * @param interp_base_address Base address of the loaded ELF interpreter.
 * @param auxv_ptr            Output pointer to store auxiliary vector. 
 * @param stack_bottom        Output pointer to store stack bottom address.
 * @return                    Pointer to the new stack location.
 */
char *setup_upper_half_stack(int argc, char **argv, int cmd_argc, char **cmd_argv2,
                      char *interp_base_address, Elf64_auxv_t **auxv_ptr, void **stack_bottom)
{
  // Get stack's size limit. We need to leave enough space between the loader
    // and the initial top of the stack. This also helps us to decide which segments
    // are upper half stack at checkpoint time.
    struct rlimit rlim;
    getrlimit(RLIMIT_STACK, &rlim);
  
    // Check for the edge case where soft limit for rlimit stack is
    // set to RLIM_INFINITY (which is -1) failing the 16-bit layout check.
    if(rlim.rlim_cur == RLIM_INFINITY){
      // FIXME: putting in a placeholder value for now as 16MB
      // This size is chosen to provide good distance between 
      //  library segments and red zone, as well as red zone and UH-stack.
      rlim.rlim_cur = 0x1000000;
    }
  
    /*
     * NOTE: In some systems, rlimit_cur for stack can be as small as 8KB.
     *  Yet mapping a bigger stack(calculated below) for UH goes unnoticed by kernel,
     *  becasue UH stack is not recognized by kernel as 'stack', thus 
     *    no kernel errors are thrown.
     *
     * FIXME:Should add check that interp_base_address + 0x2000000 not already mapped
     *  Or else, could use newer MAP_FIXED_NOREPLACE in mmap of deepCopyStack.
     *  
     * LOADER_SIZE_LIMIT is set to 0x2000000 to avoid conflict with 
     *    ld.so address space in UH.
     * Seen on MPICH-3.3.2 on Discovery (login/ssh node) CentOS-7 and Dekaksi Ubuntu-server.
     */
    char *dest_stack = deepCopyStack(argc, argv, (char *)&argc, (char *)&argv[0],
                                    cmd_argc+1, cmd_argv2,
                                    interp_base_address + rlim.rlim_cur + LOADER_SIZE_LIMIT,
                                    auxv_ptr, stack_bottom);
    lh_info->uh_stack_start = (char*) ROUND_DOWN(interp_base_address + LOADER_SIZE_LIMIT, PAGE_SIZE);
    lh_info->uh_stack_end = (char*) ROUND_UP(*stack_bottom, PAGE_SIZE);
    
    /*
     * Creating a red zone for UH stack for detecting
     *  collision  with ld.so library segments.
     * The red zone will be created (rlim.rlim_cur + PAGE_SIZE) size
     *  above the lh_info->uh_stack_start position, so that recorded
     *  lh_info->uh_stack_start still falls in the stack region.
     */
    if (create_red_zone(lh_info->uh_stack_start - rlim.rlim_cur) == NULL) {
      perror("red zone");
      exit(1);
    }

    return dest_stack;
}

/**
 * @brief Creates a red-zone below the user-space stack to detect collisions.
 *
 * This function reserves a memory page directly below the given stack address 
 *  and sets its protection to `PROT_NONE`, making it inaccessible. This ensures 
 *  that any stack overflow or unintended memory access will trigger a segmentation
 *  fault, allowing for reliable detection of stack collisions with `ld.so`.
 *
 * @param stack_addr  The base address of the user-space stack.
 * @return            A popinter to the allocated red-zone, 
 *                    or NULL on failure
 */
char *create_red_zone(char *stack_addr)
{
  char* red_zone = (char *)mmap_wrapper(
      stack_addr - PAGE_SIZE,    //one page long red-zone
      PAGE_SIZE,
      PROT_NONE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_GROWSDOWN,
      -1,
      0);

  if (red_zone == MAP_FAILED) {
    perror("red zone");
    return NULL; 
  }
  return red_zone;
}

/**
 * @brief Updates specific entries in the auxiliary vector.
 *
 * This function iterates over the auxiliary vector (`auxv`) and updates the
 *  values of `AT_PHDR`, `AT_ENTRY`, and `AT_PHNUM` to match the given parameters.
 *  These entries are crucial for dynamic linking, as they specify the program
 *  header table location, entry point, and number of program headers, respectively.
 *
 * @param av  Pointer to the auxiliary vector.
 * @param phnum Number of program headers.
 * @param phdr  Address of the program header table.
 * @param entry Entry point address of the ELF executable.
 */
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

/**
 * @brief Retrieves the offset of a symbol in an ELF interpreter, 
 *        checking debug paths if necessary.
 *
 * This function first attempts to locate the given symbol within the 
 *  specified ELF interpreter. If the symbol is not found, it checks 
 *  for a debug version of the ELF binary under `/usr/lib/debug`
 *  (as commonly used in Debian-based systems). If the debug version 
 *  exists, it attempts to locate the symbol in th efile instead.
 *
 * @param elf_interpreter Path to the ELF interpreter binary.
 * @param symbol          Name  of the symbol whose offset is to be retrieved.
 * @return                The offset of the symbol within the ELF binary. The
 *                        function asserts if the symbol is found.
 */
off_t get_symbol_offset_with_debug(const char *elf_interpreter, const char *symbol)
{
  off_t offset = get_symbol_offset(elf_interpreter, symbol);
  if (!offset) {
    char buf[256] = "/usr/lib/debug";
    buf[sizeof(buf)-1] = '\0';
    
    ssize_t rc = 0;
    rc = readlink(elf_interpreter, buf + strlen(buf), sizeof(buf) - strlen(buf) - 1);
    if (rc != -1 && access(buf, F_OK) == 0) {
      // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
    }
    offset = get_symbol_offset(buf, symbol); // elf interpreter debug path
  }
  assert(offset);
  return offset;
}

int MPI_MANA_Internal(char *dummy) {
  return 0;
}

