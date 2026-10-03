#ifndef MANA_BUILD_ID_DEBUG_H
#define MANA_BUILD_ID_DEBUG_H

#include <string>

std::string get_build_id_debug_path(const char *elf_path,
                                    const char *debug_root = nullptr);

#endif
