#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <elf.h>

#include "mem_wrapper.h"
#include "patch_trampoline.h"
#include "lower_half_api.h"
#include "logging.h"
#include "dmtcp.h"
#include "dmtcprestartinternal.h"
#include "procmapsarea.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "util.h"
#if 0
#include "mtcp_sys.h"
#include "mtcp_restart.h"
#include "mtcp_util.h"
#endif

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

#define MAX_ELF_INTERP_SZ 256

int write_lh_info_to_file();
void get_elf_interpreter(int fd, Elf64_Addr *cmd_entry,
                         char get_elf_interpreter[], void *ld_so_addr);
void *load_elf_interpreter(int fd, char elf_interpreter[],
                           void *ld_so_addr, Elf64_Ehdr *ld_so_elf_hdr_ptr);
char *map_elf_interpreter_load_segment(int fd, Elf64_Phdr phdr,
                                      void *ld_so_addr);
static void patchAuxv(Elf64_auxv_t *av, unsigned long phnum,
                      unsigned long phdr, unsigned long entry);

off_t get_symbol_offset(char *pathame, char *symbol);
// See: http://articles.manugarg.com/aboutelfauxiliaryvectors.html
//   (but unfortunately, that's for a 32-bit system)
char *deepCopyStack(int argc, char **argv,
                    unsigned long dest_argc, char **dest_argv, char *dest_stack,
                    Elf64_auxv_t **);
void *lh_dlsym(enum MPI_Fncs fnc);
unsigned long get_lh_fsaddr();
static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr);
static void restore_vdso_vvar(DmtcpCkptHeader *dmtcp_hdri, char *envrion[]);

void *ld_so_entry;
LowerHalfInfo_t lh_info = {0};

#define MTCP_RESTART_BINARY "mtcp_restart"

int main(int argc, char *argv[], char *envp[]) {
  int i;
  int restore_mode = 0;
  int cmd_argc = 0;
  char **cmd_argv;
  int cmd_fd;
  int ld_so_fd;
  void * ld_so_addr = NULL;
  Elf64_Ehdr ld_so_elf_hdr;
  Elf64_Addr cmd_entry;
  char elf_interpreter[MAX_ELF_INTERP_SZ];

  CheckAndEnableFsGsBase();
  // Initialize MPI in advance
  int rank;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  unsigned long fsaddr = 0;
  syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
  lh_info.fsaddr = (void*)fsaddr;
  lh_info.get_lh_fsaddr = (void*)get_lh_fsaddr;

  // Check arguments and setup arguments for the loader program (cmd)
  // if has "--restore", pass all arguments to mtcp_restart
  for (i = 1; i < argc; i++) {
    printf("arg %d: %s\n", i, argv[i]);
    if (strcmp(argv[i], "--restore") == 0) {
      fprintf(stderr, "restore mode\n");
      restore_mode = 1;
      break;
    }
  }
  // Restart case
  if (restore_mode) {
    DmtcpRestart dmtcpRestart(argc, argv, BINARY_NAME, MTCP_RESTART_BINARY);

    RestoreTarget *t;
    if (dmtcpRestart.restartDir.empty()) {
      t = new RestoreTarget(dmtcpRestart.ckptImages[0]);
    } else {
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

    DmtcpCkptHeader ckpt_hdr;
    int rc = read(t->fd(), &ckpt_hdr, sizeof(ckpt_hdr));
    ASSERT_EQ(rc, sizeof(ckpt_hdr));
    ASSERT_EQ(string(ckpt_hdr.ckptSignature), string(DMTCP_CKPT_SIGNATURE));

    ProcMapsArea area;
    off_t ckpt_file_pos = 0;
    ckpt_file_pos = lseek(t->fd(), 0, SEEK_CUR);
    // Reserve space for memory areas saved in the ckpt image file.
    while(1) {
      off_t cur_pos = ckpt_file_pos;
      int rc = read(t->fd(), &area, sizeof area);
      cur_pos = lseek(t->fd(), 0, SEEK_CUR);
      if (area.addr == NULL) {
        lseek(t->fd(), ckpt_file_pos, SEEK_SET);
        break;
      }
      if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
        void *mmapedat = mmap(area.addr, area.size, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                              0, 0);
        assert(mmapedat != MAP_FAILED);
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
    t->initialize();
    // Release reserved memory areas
    while(1) {
      int rc = read(t->fd(), &area, sizeof area);
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

    // If we were the session leader, become one now.
    if (t->sid() == t->pid()) {
      if (getsid(0) != t->pid()) {
        JWARNING(setsid() != -1)
          (getsid(0))(JASSERT_ERRNO)
          .Text("Failed to resotre this process as session leader.");
      }
    }

    while (1) {
      int ret = restoreMemoryArea(t->fd(), &ckpt_hdr);
      if (ret == -1) {
        break; /* end of ckpt image */
      }
    }

    /* Everything restored, close file and finish up */
    close(t->fd());

    // IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

    restore_vdso_vvar(&ckpt_hdr, envp);
    PostRestartFnPtr_t postRestart = (PostRestartFnPtr_t) ckpt_hdr.postRestartAddr;
    postRestart(0, 0);
    // The following line should not be reached.
    assert(0);
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
      fprintf(stderr, "USAGE:  %s [-a load_address] <command_line>\n", argv[0]);
      fprintf(stderr, "  (load_address is typically a multiple of 0x200000)\n");
      exit(1);
    } else if (strcmp(argv[i], "-a") == 0) {
      i++;
      ld_so_addr = (void *)strtoll(argv[i], NULL, 0);
    } else {
      // Break at the first argument of the loader program (the program name)
      cmd_argc = argc - i;
      cmd_argv = argv + i;
      break;
    }
  }
  if (ld_so_addr == NULL) {
    ld_so_addr = (void*) 0x80000000;
  }

  // Program name not provided
  if (cmd_argc == 0) {
    fprintf(stderr, "USAGE:  %s [-a load_address] <command_line>\n", argv[0]);
    fprintf(stderr, "  (load_address is typically a multiple of 0x200000)\n");
    exit(1);
  }

  // Launch case
  cmd_fd = open(cmd_argv[0], O_RDONLY);
  get_elf_interpreter(cmd_fd, &cmd_entry, elf_interpreter, ld_so_addr);
  // FIXME: The ELF Format manual says that we could pass the cmd_fd to ld.so,
  //   and it would use that to load it.
  close(cmd_fd);

  // Open the elf interpreter (ldso)
  ld_so_fd = open(elf_interpreter, O_RDONLY);
  char *interp_base_address =
    (char *)load_elf_interpreter(ld_so_fd, elf_interpreter, ld_so_addr, &ld_so_elf_hdr);
  ld_so_entry = interp_base_address  + ld_so_elf_hdr.e_entry;
  // FIXME: The ELF Format manual says that we could pass the ld_so_fd to ld.so,
  //   and it would use that to load it.
  close(ld_so_fd);

  // Insert elf interpreter before loaded program's arguments
  char *cmd_argv2[100];
  assert(cmd_argc+1 < 100);
  cmd_argv2[0] = elf_interpreter;
  for (i = 0; i < cmd_argc; i++) {
    cmd_argv2[i+1] = cmd_argv[i];
  }
  cmd_argv2[i+1] = NULL;

  // Deep copy the stack and get the auxv pointer
  Elf64_auxv_t *auxv_ptr;
  // FIXME:
  //   Should add check that interp_base_address + 0x400000 not already mapped
  //   Or else, could use newer MAP_FIXED_NOREPLACE in mmap of deepCopyStack
  char *dest_stack = deepCopyStack(argc, argv, cmd_argc+1, cmd_argv2,
                                   interp_base_address + 0x400000,
                                   &auxv_ptr);
  lh_info.uh_stack = dest_stack;

  // FIXME:
  // **************************************************************************
  // *******   elf_hdr.e_entry is pointing to kernel-loader instead of to ld.so
  // *******   ld_so_entry and interp_base_address + ld_so_elf_hdr.e_entry
  // *******     should be the same.  Eventually, rationalize this.
  // **************************************************************************
  //   AT_PHDR: "The address of the program headers of the executable."
  // elf_hdr.e_phoff is offset from begining of file.
  patchAuxv(auxv_ptr, ld_so_elf_hdr.e_phnum,
            (unsigned long)(interp_base_address + ld_so_elf_hdr.e_phoff),
            (unsigned long)interp_base_address + ld_so_elf_hdr.e_entry);
  // info->phnum, (uintptr_t)info->phdr, (uintptr_t)info->entryPoint);

  // Create new heap region to be used by RTLD
  
  void *lh_brk = sbrk(0);
  const uint64_t heapSize = 0x1000000;
  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.
  void *heap_addr = mmap_wrapper(lh_brk, heapSize, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS |
                                 MAP_NORESERVE | MAP_FIXED_NOREPLACE ,
                                 -1, 0);
  printf("emulated upper-half heal addr: %p\n", heap_addr);
  if (heap_addr == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region. Error: %s\n",
         strerror(errno));
    exit(1);
  }
  __startOfReservedHeap = heap_addr;
  __endOfReservedHeap = heap_addr + heapSize;
  // Add a guard page before the start of heap; this protects
  // the heap from getting merged with a "previous" region.
  mprotect(heap_addr, PAGE_SIZE, PROT_NONE);
  set_uh_brk((void*)((void *)heap_addr + PAGE_SIZE));
  set_end_of_heap((void*)((void *)heap_addr + heapSize));
  DLOG(INFO, "uh_brk: %p\n", heap_addr + PAGE_SIZE);

  // Insert trampolines for mmap, munmap, sbrk
  off_t mmap_offset = get_symbol_offset(elf_interpreter, "mmap");
  if (! mmap_offset) {
    char buf[256] = "/usr/lib/debug";
    buf[sizeof(buf)-1] = '\0';
    ssize_t rc = 0;
    rc = readlink(elf_interpreter, buf+strlen(buf), sizeof(buf)-strlen(buf)-1);
    if (rc != -1 && access(buf, F_OK) == 0) {
      // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
    }
    mmap_offset = get_symbol_offset(buf, "mmap"); // elf interpreter debug path
  }
#if 0
  assert(mmap_offset);
  off_t sbrk_offset = get_symbol_offset(elf_interpreter, "sbrk");
  if (! sbrk_offset) {
    char buf[256] = "/usr/lib/debug";
    buf[sizeof(buf)-1] = '\0';
    ssize_t rc = 0;
    rc = readlink(elf_interpreter, buf+strlen(buf), sizeof(buf)-strlen(buf)-1);
    if (rc != -1 && access(buf, F_OK) == 0) {
      // Debian family (Ubuntu, etc.) use this scheme to store debug symbols.
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      fprintf(stderr, "Debug symbols for interpreter in: %s\n", buf);
    }
    sbrk_offset = get_symbol_offset(buf, "sbrk"); // elf interpreter debug path
  }
  assert(sbrk_offset);
  fprintf(stderr,
          "Address of 'mmap' in memory of ld.so (run-time loader):  %p\n",
          interp_base_address + mmap_offset);
#endif

  // Patch ld.so mmap() and sbrk() functions to jump to mmap() and sbrk()
  // in libc.a of this kernel-loader program.
  patch_trampoline(interp_base_address + mmap_offset, (void*)&mmap_wrapper);
#if 0
  patch_trampoline(interp_base_address + sbrk_offset, (void*)&sbrk_wrapper);
  lh_info.sbrk = (void*)&sbrk_wrapper;
#endif
  // Setup lower-hlaf info struct for the upper-half to read from
  lh_info.mmap = (void*)&mmap_wrapper;
  lh_info.munmap = (void*)&munmap_wrapper;
  lh_info.lh_dlsym = (void*)&lh_dlsym;
  lh_info.mmap_list_fptr = (void*)&get_mmapped_list;
  lh_info.uh_end_of_heap = (void*)&get_end_of_heap;
  lh_info.set_end_of_heap = (void*)&set_end_of_heap;
  lh_info.set_uh_brk = (void*)&set_uh_brk;
  write_lh_info_to_file();
  printf("LD_PRELOAD: %s\n", getenv("LD_PRELOAD"));
  printf("UH_PRELOAD: %s\n", getenv("UH_PRELOAD"));

  // Then jump to _start, ld_so_entry, in ld.so (or call it
  //   as fnc that never returns).
  //   > ldso_entrypoint = getEntryPoint(ldso);
  //   > // TODO: Clean up all the registers?
  //   > asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
  //   >               : : "g" (newStack) : "memory");
  //   > asm volatile ("jmp *%0" : : "g" (ld_so_entry) : "memory");
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%esp; )
                : : "g" (dest_stack) : "memory");
  asm volatile ("jmp *%0" : : "g" (ld_so_entry) : "memory");
}

int write_lh_info_to_file() {
  size_t rc = 0;
  char filename[100] = "./lh_info_";
  gethostname(filename + strlen(filename), 100 - strlen(filename));
  snprintf(filename + strlen(filename), 100 - strlen(filename), "_%d", getpid());
  int fd = open(filename, O_WRONLY | O_CREAT, 0644);
  if (fd < 0) {
    DLOG(ERROR, "Could not create addr.bin file. Error: %s", strerror(errno));
    return -1;
  }

  rc = write(fd, &lh_info, sizeof(lh_info));
  if (rc < sizeof(lh_info)) {
    DLOG(ERROR, "Wrote fewer bytes than expected to addr.bin. Error: %s",
         strerror(errno));
    rc = -1;
  }
  close(fd);
  return rc;
}

void get_elf_interpreter(int fd, Elf64_Addr *cmd_entry,
                         char elf_interpreter[], void *ld_so_addr) {
  unsigned char e_ident[EI_NIDENT];
  int rc;
  rc = read(fd, e_ident, sizeof(e_ident));
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
  assert(rc == phdr.p_filesz);

  fprintf(stderr, "Interpreter (ld.so): %s\n", elf_interpreter);
}

void *load_elf_interpreter(int fd, char elf_interpreter[],
                           void *ld_so_addr, Elf64_Ehdr *ld_so_elf_hdr_ptr) {
  unsigned char e_ident[EI_NIDENT];
  int rc;
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
  return interp_base_address;
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

#define PAGE_OFFSET(x) ((x)-ROUND_DOWN(x, PAGE_SIZE))

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

int MPI_MANA_Internal(char *dummy) {
  return 0;
}

static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr)
{
  int imagefd;
  void *mmappedat;

  /* Read header of memory area into area; mtcp_readfile() will read header */
  ProcMapsArea area;

  int rc = read(fd, &area, sizeof area);
  if (area.addr == NULL) {
    return -1;
  }

  if (area.name[0] && strstr(area.name, "[heap]")
      && (VA) brk(NULL) != area.addr + area.size) {
    // JWARNING(false);
   // ("WARNING: break (%p) not equal to end of heap (%p)\n", mtcp_sys_brk(NULL), area.addr + area.size);
  }

  /* MAP_GROWSDOWN flag is required for stack region on restart to make
   * stack grow automatically when application touches any address within the
   * guard page region(usually, one page less then stack's start address).
   *
   * The end of stack is detected dynamically at checkpoint time. See
   * prepareMtcpHeader() in threadlist.cpp and ProcessInfo::growStack()
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
          /* If the current file size is smaller than the original, we map the region
          * as private anonymous. Note that with this we lose the name of the region
          * but most applications may not care.
          */
          off_t curr_size = lseek(imagefd, 0, SEEK_END);
          JASSERT(curr_size != -1);
          if ((curr_size < area.offset + area.size) && (area.prot & PROT_WRITE)) {
            JTRACE("restoring non-anonymous area %s as anonymous: %p  bytes at %p\n") (area.name) (area.size) (area.addr);
            close(imagefd);
            imagefd = -1;
            area.offset = 0;
            area.flags |= MAP_ANONYMOUS;
          }
        }
      }

      if (area.flags & MAP_ANONYMOUS) {
        JTRACE("restoring anonymous area, %p  bytes at %p\n") (area.size) (area.addr);
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
      mmappedat =
        mmap(area.addr, area.size, area.prot | PROT_WRITE,
                            area.flags | MAP_FIXED_NOREPLACE, imagefd, area.offset);

      printf("mmappedat: %p, area.addr: %p, area.endAddr%p\n", mmappedat, area.addr, area.endAddr);
      if (mmappedat != area.addr) {
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
        size_t rc = 0, count = 0;
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

NO_OPTIMIZE
static int
mremap_move(void *dest, void *src, size_t size) {
  int mtcp_sys_errno;
  if (dest == src) {
    return 0; // Success
  }
  void *rc = mremap(src, size, size, MREMAP_FIXED | MREMAP_MAYMOVE, dest);
  if (rc == dest) {
    return 0; // Success
  } else if (rc == MAP_FAILED) {
    fprintf(stderr, "***Error: failed to mremap; src->dest: %p->%p, size: 0x%x;"
            " errno: %d.\n", src, dest, size, mtcp_sys_errno);
    return -1; // Error
  } else {
    // Else 'MREMAP_MAYMOVE' forced the remap to the wrong location.  So, the
    // memory was moved to the wrong desgination.  Undo the move, and return -1.
    mremap_move(src, rc, size);
    return -1; // Error
  }
}

int mtcp_setauxval(char **evp, unsigned long int type, unsigned long int val)
{ while (*evp++ != NULL) {};
  Elf64_auxv_t *auxv = (Elf64_auxv_t *)evp;
  Elf64_auxv_t *p;
  for (p = auxv; p->a_type != AT_NULL; p++) {
    if (p->a_type == type) {
       p->a_un.a_val = val;
       return 0;
    }
  }
  return -1;
}

/* Read non-null character, return null if EOF */
char mtcp_readchar (int fd)
{
  int mtcp_sys_errno;
  char c;
  int rc;

  do {
    rc = read (fd, &c, 1);
  } while ( rc == -1 && mtcp_sys_errno == EINTR );
  if (rc <= 0) return (0);
  return (c);
}

/* Read decimal number, return value and terminating character */
char mtcp_readdec (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read decimal number, return value and terminating character */
char mtcp_readhex (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
      if ((c >= '0') && (c <= '9')) c -= '0';
    else if ((c >= 'a') && (c <= 'f')) c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F')) c -= 'A' - 10;
    else break;
    v = v * 16 + c;
  }
  *value = (VA)v;
  return (c);
}

int mtcp_readmapsline (int mapsfd, Area *area)
{
  int mtcp_sys_errno __attribute__((unused));
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = mtcp_readhex (mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0)) return (0);
    goto skipeol;
  }
  c = mtcp_readhex (mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = mtcp_readchar (mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = mtcp_readchar (mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = mtcp_readchar (mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = mtcp_readchar (mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = mtcp_readchar (mapsfd);
  if (c != ' ') goto skipeol;

  c = mtcp_readhex (mapsfd, (VA *)&offset);
  if (c != ' ') goto skipeol;
  area -> offset = offset;

  c = mtcp_readhex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = mtcp_readhex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = mtcp_readdec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = mtcp_readchar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = mtcp_readchar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> endAddr = endaddr;
  area -> size = endaddr - startaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  area->devmajor = devmajor;
  area->devminor = devminor;
  area->inodenum = inodenum;
  return (1);

skipeol:
  fprintf(stderr, "ERROR:  mtcp readmapsline*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0')) {
    c = mtcp_readchar (mapsfd);
    fprintf(stderr, "%c", c);
  }
  fprintf(stderr, ">\n");
  abort ();
  return (0);  /* NOTREACHED : stop compiler warning */
}

NO_OPTIMIZE
static void
restore_vdso_vvar(DmtcpCkptHeader *dmtcp_hdr, char *environ[])
{
  int mtcp_sys_errno;

  // Computer vdso and vvar
  Area area;
  char* currentVdsoStart = NULL;
  char* currentVdsoEnd = NULL;
  char* currentVvarStart = NULL;
  char* currentVvarEnd = NULL;

  int mapsfd = open("/proc/self/maps", O_RDONLY, 0777);

  if (mapsfd < 0) {
    printf("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (strcmp(area.name, "[vdso]") == 0) {
      // Do not unmap vdso.
      currentVdsoStart = area.addr;
      currentVdsoEnd = area.endAddr;
      fprintf(stderr, "***INFO: vDSO found (%p..%p)\n original vDSO: (%p..%p)\n",
              area.addr, area.endAddr, dmtcp_hdr->vdsoStart, dmtcp_hdr->vdsoEnd);
    }
#if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
    else if (area.addr == 0xfffe0000 && area.size == 4096) {
      // It's a vdso page from a time before Linux displayed the annotation.
      // Do not unmap vdso.
    }
#endif /* if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
          */
    else if (strcmp(area.name, "[vvar]") == 0) {
      // Do not unmap vvar.
      currentVvarStart = area.addr;
      currentVvarEnd = area.endAddr;
    }
  }

  close(mapsfd);

  if (currentVdsoEnd - currentVdsoStart != dmtcp_hdr->vdsoEnd - dmtcp_hdr->vdsoStart) {
    printf("***Error: vdso size mismatch.\n");
    abort();
  }

  if (currentVvarEnd - currentVvarStart != dmtcp_hdr->vvarEnd - dmtcp_hdr->vvarStart) {
    printf("***Error: vvar size mismatch. Current: %p..%p; existing %p..%p\n",
      currentVvarEnd, currentVvarStart, dmtcp_hdr->vvarEnd, dmtcp_hdr->vvarStart);
    abort();
  }

  if (currentVvarStart != NULL) {
    int rc = mremap_move((void*)dmtcp_hdr->vvarStart,
                         currentVvarStart,
                         currentVvarEnd - currentVvarStart);
    if (rc == -1) {
      printf("***Error: failed to restore vvar to pre-ckpt location.");
      abort();
    }
  }

  if (currentVdsoStart != NULL) {
    int rc = mremap_move((void*)dmtcp_hdr->vdsoStart,
                         currentVdsoStart,
                         currentVdsoEnd - currentVdsoStart);
    if (rc == -1) {
      printf("***Error: failed to restore vdso to pre-ckpt location.");
      abort();
    }
    mtcp_setauxval(environ, AT_SYSINFO_EHDR,
                   (unsigned long int) dmtcp_hdr->vdsoStart);
  }
}

unsigned long get_lh_fsaddr() {
  unsigned long fsaddr = 0;
  syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
  return fsaddr;
}