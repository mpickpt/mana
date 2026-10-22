#ifndef MANA_BUILD_ID_DEBUG_H
#define MANA_BUILD_ID_DEBUG_H

#include <sys/types.h>
#include <string>

std::string get_build_id_debug_path(
  const char *elf_path, const char *debug_root = "/usr/lib/debug");

off_t get_symbol_offset_with_debug(
  const char *elf_path, const char *symbol,
  const char *debug_root = "/usr/lib/debug");

#endif
