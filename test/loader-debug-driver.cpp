#include "build-id-debug.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc == 4 && strcmp(argv[1], "path") == 0) {
    puts(get_build_id_debug_path(argv[2], argv[3]).c_str());
    return 0;
  }
  if ((argc == 4 || argc == 5) && strcmp(argv[1], "lookup") == 0) {
    const char *root = argc == 5 ? argv[4] : "/usr/lib/debug";
    const off_t offset = get_symbol_offset_with_debug(argv[2], argv[3], root);
    printf("%lld\n", static_cast<long long>(offset));
    return offset == 0;
  }
  fprintf(stderr, "Usage: %s path ELF DEBUG_ROOT\n", argv[0]);
  fprintf(stderr, "       %s lookup ELF SYMBOL [DEBUG_ROOT]\n", argv[0]);
  return 2;
}
