#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <elf.h>

#include <mmap_wrapper.h>
#include <sbrk_wrapper.h>
#include <patch_trampoline.h>

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
#define PAGE_SIZE 0x1000LL

LowerHalfInfo_t lh_info = {0};
static void* MPI_Fnc_Ptrs[] = {
  NULL,
  FOREACH_FNC(GENERATE_FNC_PTR)
  NULL,
};

void*
lh_dlsym(enum MPI_Fncs fnc)
{
  if (fnc < MPI_Fnc_NULL || fnc > MPI_Fnc_Invalid) {
    return NULL;
  }
  return MPI_Fnc_Ptrs[fnc];
}

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

int main(int argc, char *argv[], char *envp[]) {
  int i;
  int cmd_argc = 0;
  char **cmd_argv;
  int cmd_fd;
  int ld_so_fd;
  void * ld_so_addr = NULL;
  Elf64_Ehdr ld_so_elf_hdr;
  Elf64_Addr cmd_entry;
  void *ld_so_entry;
  char elf_interpreter[MAX_ELF_INTERP_SZ];

  // Check arguments and setup arguments for the loader program (cmd)
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

  // Program name not provided
  if (cmd_argc == 0) {
    fprintf(stderr, "USAGE:  %s [-a load_address] <command_line>\n", argv[0]);
    fprintf(stderr, "  (load_address is typically a multiple of 0x200000)\n");
    exit(1);
  }

  // Open the program executable and get the elf interpreter
  // Question: What if the program is a script?
  cmd_fd = open(cmd_argv[0], O_RDONLY);
  get_elf_interpreter(cmd_fd, &cmd_entry, elf_interpreter, ld_so_addr);
  // FIXME: The ELF Format manual says that we could pass the cmd_fd to ld.so,
  //   and it would use that to load it.
  close(cmd_fd);

  // Open the elf interpreter (ldso)
  ld_so_fd = open(elf_interpreter, O_RDONLY);
  char *interp_base_address =
    load_elf_interpreter(ld_so_fd, elf_interpreter, ld_so_addr, &ld_so_elf_hdr);
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
  const uint64_t heapSize = 100 * PAGE_SIZE;
  // We go through the mmap wrapper function to ensure that this gets added
  // to the list of upper half regions to be checkpointed.
  void *heap_addr = mmap_wrapper(0, heapSize, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (heap_addr == MAP_FAILED) {
    DLOG(ERROR, "Failed to mmap region. Error: %s\n",
         strerror(errno));
    return NULL;
  }
  // Add a guard page before the start of heap; this protects
  // the heap from getting merged with a "previous" region.
  mprotect(heap_addr, PAGE_SIZE, PROT_NONE);
  setUhBrk((void*)((VA)heap_addr + PAGE_SIZE));
  setEndOfHeap((void*)((VA)heap_addr + heapSize));

  // Insert trampolines for mmap, munmap, sbrk
  off_t mmap_offset, sbrk_offset;
#if UBUNTU
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
  sbrk_offset = get_symbol_offset(buf, "sbrk"); // elf interpreter debug path
#else
  mmap_offset = get_symbol_offset(elf_interpreter, "mmap");
  sbrk_offset = get_symbol_offset(elf_interpreter, "sbrk");
#endif
  assert(mmap_offset);
  assert(sbrk_offset);
#ifdef DEBUG
  fprintf(stderr,
          "Address of 'mmap' in memory of ld.so (run-time loader):  %p\n",
          interp_base_address + mmap_offset);
  fprintf(stderr,
          "Address of 'sbrk' in memory of ld.so (run-time loader):  %p\n",
          interp_base_address + sbrk_offset);
#endif
  // Patch ld.so mmap() and sbrk() functions to jump to mmap() and sbrk()
  // in libc.a of this kernel-loader program.
  patch_trampoline(interp_base_address + mmap_offset, &mmap_wrapper);
  patch_trampoline(interp_base_address + sbrk_offset, &sbrk_wrapper);

  // Setup lower-hlaf info struct for the upper-half to read from
  unsigned long fsaddr = 0;
  syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
  lh_info.sbrk = (void*)&sbrk_wrapper;
  lh_info.mmap = (void*)&mmap_wrapper;
  lh_info.munmap = (void*)&munmap_wrapper;
  lh_info.lh_dlsym = (void*)&lh_dlsym;
  lh_info.mmap_list_fptr = (void*)&get_mmapped_list;
  lh_info.uh_end_of_heap = (void*)&get_end_of_heap;
  lh_info.fsaddr = (void*)fsaddr;

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
  char filename[100];
  snprintf(filename, 100, "./lh_info_%d", getpid());
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

// FIXME: 0x1000 is one page; Use sysconf(PAGESIZE) instead.
#define ROUND_DOWN(x) ((unsigned long)(x) \
                       & ~(unsigned long)(0x1000-1))
#define ROUND_UP(x) ((unsigned long)((x) + (0x1000-1)) \
                     & ~(unsigned long)(0x1000-1))
#define PAGE_OFFSET(x) ((x)-ROUND_DOWN(x))

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
                        (unsigned long)ld_so_addr + ROUND_DOWN(phdr.p_vaddr) :
                        (unsigned long)base_address + ROUND_DOWN(phdr.p_vaddr));
  // addr was rounded down, above.  To compensate, we round up here by a
  //   fractional page for phdr.p_vaddr.
  unsigned long len = phdr.p_memsz + PAGE_OFFSET(phdr.p_vaddr);
  unsigned long offset = ROUND_DOWN(phdr.p_offset);

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
    rc2 = mmap((void *)addr, len, prot, MAP_PRIVATE, fd, offset);
  } else {
    assert((char *)addr+len >=
           (char *)base_address + phdr.p_vaddr + phdr.p_memsz);
    rc2 = mmap((void *)addr, len, prot, MAP_PRIVATE|MAP_FIXED, fd, offset);
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
    memset(endBSS, '\0', (char *)ROUND_UP(endBSS) - endBSS);
  }

  if (first_time) {
    first_time = 0;
    base_address = rc2;
  }
  return base_address;
}
