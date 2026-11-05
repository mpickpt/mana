#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h> // for PATH_MAX
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <elf.h>

static int readElfSection(int , int , const Elf64_Ehdr* ,
                          Elf64_Shdr* , char **);
static int expandDebugFile(char *debugLibName,
                           const char *dir, const char *debugName);

off_t get_symbol_offset(const char *pathname, const char *symbol) {
  unsigned char e_ident[EI_NIDENT];
  int rc;
  int symtab_found = 0;
  int foundDebugLib = 0;
  char debugLibName[PATH_MAX] = {0};
  char *shsectData = NULL;

  // FIXME:  Rohan's version wraps the rest of this function
  //         in 'while (retries < 2) { ... }' and then uses two
  //         or three tries to find the correct SHT_STRTAB (namely, ".strtab").

  int fd = open(pathname, O_RDONLY);
  if (fd == -1) {
    return 0; // 0 means pathname not found
  }

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

  // Get start of symbol table and string table
  Elf64_Off shoff = elf_hdr.e_shoff;
  Elf64_Shdr sect_hdr;
  Elf64_Shdr symtab;
  Elf64_Sym symtab_entry;
  char strtab[200000];
  int i;
  // First, read the data from the shstrtab section
  // This section contains the strings corresponding to the section names
  rc = readElfSection(fd, elf_hdr.e_shstrndx, &elf_hdr, &sect_hdr, &shsectData);
  lseek(fd, shoff, SEEK_SET);
  for (i = 0; i < elf_hdr.e_shnum; i++) {
    rc = read(fd, &sect_hdr, sizeof sect_hdr);
    assert(rc == sizeof(sect_hdr));
    if (sect_hdr.sh_type == SHT_SYMTAB) {
      symtab = sect_hdr;
      symtab_found = 1;
    } else if (sect_hdr.sh_type == SHT_STRTAB &&
               !strcmp(&shsectData[sect_hdr.sh_name], ".strtab")) {
      // Note that there are generally three STRTAB sections in ELF binaries:
      // .dynstr, .shstrtab, and .strtab; We only care about the strtab section.
      int fd2 = open(pathname, O_RDONLY);
      lseek(fd2, sect_hdr.sh_offset, SEEK_SET);
      assert(sect_hdr.sh_size < sizeof(strtab));
      rc = read(fd2, strtab, sect_hdr.sh_size);
      assert(rc == sect_hdr.sh_size);
      close(fd2);
    } else if (sect_hdr.sh_type == SHT_PROGBITS &&
               !strcmp(&shsectData[sect_hdr.sh_name], ".gnu_debuglink")) {
      // If it's the ".gnu_debuglink" section, we read it to figure out
      // the path to the debug symbol file
      Elf64_Shdr tmp;
      char *debugName = NULL;
      // FIXME:  Either expand this inline (if short), or more likely
      //         use readElfSection() also in case of ".strtab", above
      //         (like Rohan's version).
      rc = readElfSection(fd, i, &elf_hdr, &tmp, &debugName);
      assert(debugName);
      // All the places where the debug symbol file can hide are here:
      //   http://sourceware.org/gdb/onlinedocs/gdb/Separate-Debug-Files.html
      // Some versions of Linux store the debug symbol file as follows:
      //   Debian/Ubuntu: /lib/debug/.build-id exists
      //            /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 : no symtab
      //   CentOS: /lib64/ld-2.17.so : has symtab (symtab_found will be set)
      //           /lib/ld-linux.so.2 -> /lib64/ld-2.17.so
      //   SUSE: [same as CentOS, but ld-31.so for SUSE release 15.4)
      //   Rocky Linux 8.8: /lib64/ld-2.28.so : no symtab
      //       /lib/ld-linux.so.2 -> /lib64/ld-2.28.so
      //       /usr/lib/debug/lib64/libc-2.28.so-2.28-236.el8_9.7.x86-64.debug 
      //       Package glibc-debuginfo-*.rpm exists: provides libc-*.debug
      if (access("/lib/debug/.build-id", F_OK) == 0) { // If Debian/Ubuntu
        // Debian variants use separate debug symbol file: /lib/debug/.build-id
        // The file below is the older hierarchy.  Now it uses .build-id.
        snprintf(debugLibName, sizeof debugLibName, "%s/%s",
                 "/usr/lib/debug/lib/x86_64-linux-gnu", debugName);
        if (! expandDebugFile(debugLibName,
                              "/lib/debug/.build-id", debugName)) {
          fprintf(stderr,
            "\nsymtab was stripped from ld-linux.so file, and no debug file\n"
            "was found in .build-id.  Consider installing libc6-dbg package,\n"
            "or else build a new libc6 from source.  In the latter case,\n"
            "set LD_LIBRARY_PATH to your locally built ld-linux.so\n\n");
          abort();
        }
        close(fd);
        fd = open(debugLibName, O_RDONLY);
        assert(fd != -1);
        return get_symbol_offset(debugLibName, symbol);
      }
      free(debugName);
      foundDebugLib = 1;
    }
  }
  if (! symtab_found) {
    close(fd);
    return 0; // No symbol table found.
  }

  // Move to beginning of symbol table
  lseek(fd, symtab.sh_offset, SEEK_SET);
  for ( ; lseek(fd, 0, SEEK_CUR) - symtab.sh_offset < symtab.sh_size ; ) {
    rc = read(fd, &symtab_entry, sizeof symtab_entry);
    assert(rc == sizeof(symtab_entry));
    if (strcmp(strtab + symtab_entry.st_name, symbol) == 0) {
      // found address as offset from base address
      close(fd);
      return symtab_entry.st_value;
    }
  }

  close(fd);
  fprintf(stderr, "*** FAILED TO FIND symbol: %s\n", symbol);
  exit(1);
}


static int
readElfSection(int fd, int sidx, const Elf64_Ehdr *ehdr,
               Elf64_Shdr *shdr, char **data)
{
  off_t currOff = lseek(fd, 0, SEEK_CUR);
  off_t sidx_off = ehdr->e_shentsize * sidx + ehdr->e_shoff;
  lseek(fd, sidx_off, SEEK_SET);
  int rc = read(fd, shdr, sizeof *shdr);
  assert(rc == sizeof *shdr);
  rc = lseek(fd, shdr->sh_offset, SEEK_SET);
  if (rc > 0) {
    *data = (char *)malloc(shdr->sh_size);
    rc = lseek(fd, shdr->sh_offset, SEEK_SET);
    rc = read(fd, *data, shdr->sh_size);
    assert(rc == shdr->sh_size);
  }
  lseek(fd, currOff, SEEK_SET);
  return *data != NULL ? 0 : -1;
}


static int expandDebugFile(char *debugLibName,
                           const char *dir, const char *debugName) {
  DIR *directory = opendir(dir);
  if (directory == NULL) {
    return 0;
  }
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type == DT_DIR &&
        (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)) {
      continue;
    } else if (entry->d_type == DT_DIR) {
      char dir2[PATH_MAX];
      strncpy(dir2, dir, PATH_MAX);
      strncat(dir2, "/", PATH_MAX - strlen(dir2) - 1);
      strncat(dir2, entry->d_name, PATH_MAX - strlen(entry->d_name) - 1);
      if (expandDebugFile(debugLibName, dir2, debugName)) {
        return 1; // found a match
      }
      // list_files_recursively(entry->d_name);
    } else {
      if (strcmp(entry->d_name, debugName) == 0) {
        snprintf(debugLibName, PATH_MAX, "%s/%s", dir, entry->d_name);
        return 1; // found a match
      }
    }
  }
  closedir(directory);
  return 0; // no match
}


#ifdef STANDALONE
int main() {
  off_t mmap_offset = get_symbol_offset("/lib64/ld-linux-x86-64.so.2", "mmap");
  if (! mmap_offset) mmap_offset =
         get_symbol_offset("/usr/lib/debug/lib64/ld-linux-x86-64.so.2", "mmap");
  if (mmap_offset == 0) {
    fprintf(stderr, "*** FAILED TO FIND mmap offset\n");
    return 1;
  }
  fprintf(stderr, "mmap offset: 0x%lx\n", mmap_offset);
  fprintf(stderr, " (Add this to base_address of first LOAD segm. for addr)\n");
  return 0;
}
#endif
