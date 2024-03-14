#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define ROUND_DOWN(x) ((unsigned long)(x) \
                       & ~(unsigned long)(0x1000-1))

void patch_trampoline(void *from_addr, void *to_addr) {
#if defined(__x86_64__)
  unsigned char asm_jump[] = {
    // mov    $0x1234567812345678,%rax
    0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
    // jmpq   *%rax
    0xff, 0xe0
  };
  // Beginning of address in asm_jump:
  const int addr_offset = 2;
#elif defined(__i386__)
    static unsigned char asm_jump[] = {
      0xb8, 0x78, 0x56, 0x34, 0x12, // mov    $0x12345678,%eax
      0xff, 0xe0                    // jmp    *%eax
  };
  // Beginning of address in asm_jump:
  const int addr_offset = 1;
#else
# error "architecture not supported"
#endif

  unsigned long pagesize = sysconf(_SC_PAGESIZE);
  void *page_base = (void *)ROUND_DOWN((unsigned long)from_addr);
  int page_length = pagesize;
  if ((char*)from_addr + sizeof(asm_jump) - (char*)page_base > pagesize) {
    // The patching instructions cross page boundary.  View page as double size.
    page_length = 2 * pagesize;
  }

  int rc = mprotect(page_base, page_length,
                    PROT_READ | PROT_WRITE | PROT_EXEC);
  if (rc == -1) { perror("mprotect"); exit(1); }
  memcpy(from_addr, asm_jump, sizeof(asm_jump));
  memcpy(from_addr + addr_offset, &to_addr, sizeof(&to_addr));
  rc = mprotect(page_base, page_length, PROT_READ | PROT_EXEC);
  if (rc == -1) { perror("mprotect"); exit(1); }
}

#ifdef STANDALONE
void foo() {
  printf("Inside foo().  Patching failed.\n");
  exit(1);
}
void bar() {
  printf("Inside bar().  Patching succeeded.\n");
}

int main() {
  printf("Patching foo() to jump to bar(), which returns back to main.\n");
  patch_trampoline(&foo, &bar);
  foo();
  printf("Returned back to main after call to foo().\n");
}
#endif
