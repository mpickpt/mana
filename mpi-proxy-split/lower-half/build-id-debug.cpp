#include "build-id-debug.h"

#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

off_t get_symbol_offset(const char *pathname, const char *symbol);

namespace {

bool read_exact_at(int fd, void *buffer, size_t size, off_t offset)
{
  char *cursor = static_cast<char *>(buffer);
  while (size > 0) {
    ssize_t count = pread(fd, cursor, size, offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return false;
    }
    cursor += count;
    size -= count;
    offset += count;
  }
  return true;
}

std::string read_build_id(int fd)
{
  struct stat status;
  Elf64_Ehdr header;
  if (fstat(fd, &status) != 0 || status.st_size < 0 ||
      !read_exact_at(fd, &header, sizeof(header), 0)) {
    return "";
  }
  const uint16_t endian = 1;
  const unsigned char native_data =
    *reinterpret_cast<const unsigned char *>(&endian) == 1
      ? ELFDATA2LSB : ELFDATA2MSB;
  const uint64_t file_size = status.st_size;
  if (memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
      header.e_ident[EI_CLASS] != ELFCLASS64 ||
      header.e_ident[EI_DATA] != native_data ||
      header.e_ident[EI_VERSION] != EV_CURRENT ||
      header.e_version != EV_CURRENT ||
      header.e_phentsize != sizeof(Elf64_Phdr) ||
      header.e_phnum == PN_XNUM || header.e_phoff > file_size ||
      header.e_phnum > (file_size - header.e_phoff) / sizeof(Elf64_Phdr)) {
    return "";
  }

  for (size_t index = 0; index < header.e_phnum; ++index) {
    Elf64_Phdr segment;
    const off_t offset = header.e_phoff + index * sizeof(segment);
    if (!read_exact_at(fd, &segment, sizeof(segment), offset)) {
      return "";
    }
    if (segment.p_type != PT_NOTE || segment.p_offset > file_size ||
        segment.p_filesz > file_size - segment.p_offset) {
      continue;
    }
    uint64_t cursor = segment.p_offset;
    const uint64_t end = cursor + segment.p_filesz;
    while (end - cursor >= sizeof(Elf64_Nhdr)) {
      Elf64_Nhdr note;
      if (!read_exact_at(fd, &note, sizeof(note), cursor)) {
        return "";
      }
      cursor += sizeof(note);
      const uint64_t name_size = (uint64_t(note.n_namesz) + 3) & ~uint64_t(3);
      const uint64_t desc_size = (uint64_t(note.n_descsz) + 3) & ~uint64_t(3);
      if (name_size > end - cursor || desc_size > end - cursor - name_size) {
        break;
      }
      char name[4];
      if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == sizeof(name) &&
          note.n_descsz > 0 &&
          read_exact_at(fd, name, sizeof(name), cursor) &&
          memcmp(name, "GNU", sizeof(name)) == 0) {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        for (size_t byte = 0; byte < note.n_descsz; ++byte) {
          unsigned char value;
          if (!read_exact_at(fd, &value, 1, cursor + name_size + byte)) {
            return "";
          }
          result += hex[value >> 4];
          result += hex[value & 15];
        }
        return result;
      }
      cursor += name_size + desc_size;
    }
  }
  return "";
}

std::string debug_directory(const char *root)
{
  std::string result(root && *root ? root : "/usr/lib/debug");
  while (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

} // namespace

std::string
get_build_id_debug_path(const char *elf_path, const char *debug_root)
{
  if (!elf_path || !*elf_path) {
    return "";
  }
  int fd = open(elf_path, O_RDONLY);
  if (fd < 0) {
    return "";
  }
  const std::string build_id = read_build_id(fd);
  close(fd);
  if (build_id.size() < 3) {
    return "";
  }
  return debug_directory(debug_root) + "/.build-id/" +
         build_id.substr(0, 2) + "/" + build_id.substr(2) + ".debug";
}

off_t get_symbol_offset_with_debug(const char *elf_path, const char *symbol,
                                 const char *debug_root)
{
  off_t offset = get_symbol_offset(elf_path, symbol);
  if (offset) {
    return offset;
  }

  std::string conventional_path;
  char target[PATH_MAX];
  const ssize_t length = readlink(elf_path, target, sizeof(target) - 1);
  if (length >= 0) {
    target[length] = '\0';
    conventional_path = debug_directory(debug_root) + target;
    if (access(conventional_path.c_str(), R_OK) == 0) {
      offset = get_symbol_offset(conventional_path.c_str(), symbol);
      if (offset) {
        fprintf(stderr, "Debug symbols for interpreter in: %s\n",
                conventional_path.c_str());
        return offset;
      }
    }
  }

  const std::string build_id_path =
    get_build_id_debug_path(elf_path, debug_root);
  if (!build_id_path.empty() && access(build_id_path.c_str(), R_OK) == 0) {
    offset = get_symbol_offset(build_id_path.c_str(), symbol);
    if (offset) {
      fprintf(stderr, "Debug symbols for interpreter in: %s\n",
              build_id_path.c_str());
      return offset;
    }
  }

  fprintf(stderr, "Unable to find symbol %s in %s or its debug files.\n",
          symbol, elf_path);
  if (!conventional_path.empty()) {
    fprintf(stderr, "Tried: %s\n", conventional_path.c_str());
  }
  if (!build_id_path.empty()) {
    fprintf(stderr, "Tried: %s\n", build_id_path.c_str());
  }
  assert(offset);
  return offset;
}
