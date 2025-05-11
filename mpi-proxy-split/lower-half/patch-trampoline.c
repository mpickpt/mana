#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define ROUND_DOWN(x) ((unsigned long)(x) \
                       & ~(unsigned long)(sysconf(_SC_PAGESIZE)-1))

#ifdef __aarch64__
void clear_icache(void *beg, void *end);
#endif

void patch_trampoline(void *from_addr, void *to_addr) {
#if defined(__x86_64__)
  unsigned char asm_jump[] = {
    // mov $0x1234567812345678, %rax
    0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
    // jmpq   *%rax
    0xff, 0xe0
  };
  // Beginning of address in asm_jump:
  const int addr_offset = 2; // 'mov' is 2 bytes in binary
#elif defined(__i386__)
  unsigned char asm_jump[] = {
    0xb8, 0x78, 0x56, 0x34, 0x12, // mov    $0x12345678,%eax
    0xff, 0xe0                    // jmp    *%eax
  };
  // Beginning of address in asm_jump:
  const int addr_offset = 1; // 'mov' is 1 byte in binary
#elif defined(__aarch64__)
  unsigned char asm_jump[] = {
    // 0x58, 0x00, 0x00, 0x48, 0xd6, 0x1f, 0x01, 0x00
    0x48, 0x00, 0x00, 0x58, 0x00, 0x01, 0x1f, 0xd6,
    0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78 // dword
  };
  // Beginning of address in asm_jump (offset of dword):
  const int addr_offset = 8;
  // Remember that in AArch64, stack-pointer must be 128-bit (16-byte) aligned.
   // This is the assembly, converted to binary using 'objdump -d a.out'
   // SEE: https://stackoverflow.com/questions/44949124/absolute-jump-with-a-pc-relative-data-source-aarch64
   // For testing in GDB, after "ldr", do: (gdb) set $x8 = &bar
   /* asm("ldr x8, .+8"); // Add 8 to pc, store in x8
    * asm("br x8"); // jump to the 8-byte word
    * asm("target: .dword 0x1234567812345678");
    */
#elif defined(__riscv)
  unsigned char asm_jump[] = {
    0x97, 0x02, 0x00, 0x00, 0xb1, 0x02, 0x83, 0xb2, 0x02, 0x00, 0x82, 0x82,
    0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78 // dword
  };
  // Beginning of address in asm_jump (offset of dword):
  const int addr_offset = 12;
  // This is the assembly, converted to binary using 'objdump -d a.out'
  /* asm("auipc t0, 0"); // Add 0 to pc, store in t0
   * // RISC-V instructions are 16- or 32-bit.
   * asm("addi t0, t0, 12"); // addr of auipc is in t0; Add 10 to reach dword
   * asm("ld t0, 0(t0)");    // load the double word
   * asm("jr t0"); // jump to the double word
   * asm(".dword 0x1234567812345678");
   */
# if 0
   11154:  00000297  auipc   t0,0x0                          
   11158:  02b1      addi    t0,t0,10 # 1115e <patch_trampoline+0x3a>
   1115a:  0002b283  ld      t0,0(t0)                        
   1115e:  8282      jr      t0 
# endif
#else
# error "architecture not supported"
#endif

  unsigned long pagesize = sysconf(_SC_PAGESIZE);
  void *page_base = (void *)ROUND_DOWN((unsigned long)from_addr);
  int page_length = pagesize;
  if ((char *)from_addr + sizeof(asm_jump) - (char *)page_base > pagesize) {
    // The patching instructions cross page boundary.  View page as double size.
    page_length = 2 * pagesize;
  }

  int rc = mprotect(page_base, page_length,
                    PROT_READ | PROT_WRITE | PROT_EXEC);
  if (rc == -1) { perror("mprotect"); exit(1); }
  memcpy(from_addr, asm_jump, sizeof(asm_jump));
  memcpy((void *)((char *)from_addr + addr_offset), (void *)&to_addr,
                                                    sizeof(to_addr));
  rc = mprotect(page_base, page_length, PROT_READ | PROT_EXEC);
  if (rc == -1) { perror("mprotect"); exit(1); }
#ifdef __aarch64__
  // ARM64 has an especially weak memory model; invalidate L1 icache to refill.
  // See: DMTCP:src/membarrier.h (WMV and IMB)
  asm volatile ("dsb ishst" : : : "memory"); // Wait until mods reach cache
  asm volatile (".arch armv8.1-a \n\t dsb sy" : : : "memory");
  asm volatile (".arch armv8.1-a \n\t isb sy" : : : "memory");
  // On ARMv8 (CPU Implementer 0x41), STANDALONE succeeds even without
  //   this call to clear_icache, but only if clear_icache is defined.
  //   Presumably, something abbout address alignment and we got lucky.
  clear_icache(from_addr, from_addr+sizeof(asm_jump));
#endif
}

#ifdef __aarch64__
// Copied from DMTCP/src/mtcp/mtcp_restart.c
// From Dynamo RIO, file:  dr_helper.c
// See https://github.com/DynamoRIO/dynamorio/wiki/AArch64-Port
//   (self-modifying code) for background.
# define ALIGN_FORWARD(addr,align) (void *)(((unsigned long)addr + align - 1) & ~(unsigned long)(align-1))
# define ALIGN_BACKWARD(addr,align) (void *)((unsigned long)addr & ~(unsigned long)(align-1))
void
clear_icache(void *beg, void *end)
{
    static size_t cache_info = 0;
    size_t dcache_line_size;
    size_t icache_line_size;
    typedef unsigned int* ptr_uint_t;
    ptr_uint_t beg_uint = (ptr_uint_t)beg;
    ptr_uint_t end_uint = (ptr_uint_t)end;
    ptr_uint_t addr;

    if (beg_uint >= end_uint)
        return;

    /* "Cache Type Register" contains:
     * CTR_EL0 [31]    : 1
     * CTR_EL0 [19:16] : Log2 of number of 4-byte words in smallest dcache line
     * CTR_EL0 [3:0]   : Log2 of number of 4-byte words in smallest icache line
     */
    if (cache_info == 0)
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(cache_info));
    dcache_line_size = 4 << (cache_info >> 16 & 0xf);
    icache_line_size = 4 << (cache_info & 0xf);

    /* Flush data cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, dcache_line_size);
    do {
        __asm__ __volatile__("dc cvau, %0" : : "r"(addr) : "memory");
        addr += dcache_line_size;
    } while ((char *)addr < (char *)ALIGN_FORWARD(end_uint, dcache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Invalidate instruction cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, icache_line_size);
    do {
        __asm__ __volatile__("ic ivau, %0" : : "r"(addr) : "memory");
        addr += icache_line_size;
    } while ((char *)addr < (char *)ALIGN_FORWARD(end_uint, icache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Instruction Synchronization Barrier */
    __asm__ __volatile__("isb" : : : "memory");
}
#endif

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
