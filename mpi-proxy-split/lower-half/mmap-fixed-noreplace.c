#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

void *mmap_fixed_noreplace(void *addr, size_t length, int prot, int flags,
                           int fd, off_t offset) {
#ifdef MAP_FIXED_NOREPLACE
  static int has_map_fixed_noreplace = -1;
#else
  static int has_map_fixed_noreplace = 0; // macro undefined; we don't have it
# define MAP_FIXED_NOREPLACE (-1) // Stop compiler from complaining.
#endif

  if (has_map_fixed_noreplace == -1) {
    long pagesize = sysconf(_SC_PAGESIZE); 
    int test_flag = MAP_PRIVATE | MAP_ANONYMOUS;
    void *addr = mmap(NULL, pagesize, 0, test_flag, -1, 0); // No clobber memory
    if (addr == MAP_FAILED) { return addr; }
    void *addr2 = mmap(addr, pagesize, 0, test_flag | MAP_FIXED_NOREPLACE,
                       -1, 0);
    if (addr2 == MAP_FAILED && errno == EEXIST) {
      has_map_fixed_noreplace = 1;
    } else {
      assert(addr2 == MAP_FAILED);
      has_map_fixed_noreplace = 0;
    }
    assert(munmap(addr, pagesize) == 0); // We now have our answer; unmap it.
  }

  if (flags & MAP_FIXED) {flags ^= MAP_FIXED;} // Don't clobber memory.
  if (has_map_fixed_noreplace) {
    flags |= MAP_FIXED_NOREPLACE;
    return mmap(addr, length, prot, flags, fd, offset);
  } else {
    assert(has_map_fixed_noreplace == 0);
#if MAP_FIXED_NOREPLACE != -1
    if (flags & MAP_FIXED_NOREPLACE) {flags ^= MAP_FIXED_NOREPLACE;}
#endif
    void *addr2 = mmap(addr, length, prot, flags, fd, offset);

    if (addr2 == addr) {
      return addr; // Success!
    } else if (addr2 != MAP_FAILED) { // The memory region already exists.
      assert(addr2 != addr);
      assert(munmap(addr2, length) == 0);
      errno = EEXIST;
      return MAP_FAILED;
    } else { // The mmap really did fail.  Keep the errno for caller.
      assert(addr2 == MAP_FAILED);
      return MAP_FAILED;
    }
  }
}

#ifdef STANDALONE
// Returns on success; asserts on error.
int main() {
  long pagesize = sysconf(_SC_PAGESIZE); 
  errno = 0;
  int test_flag = MAP_PRIVATE | MAP_ANONYMOUS;
  void *addr = mmap(NULL, pagesize, 0, test_flag, -1, 0); // No clobber memory
  assert(addr != MAP_FAILED);

  void *addr2 = mmap_fixed_noreplace(addr, pagesize, 0, test_flag, -1, 0);
  assert(addr2 == MAP_FAILED && errno == EEXIST);

  addr2 = mmap(addr, pagesize, 0, test_flag, -1, 0); // Test internals of fnc
  assert(addr2 != MAP_FAILED && addr2 != addr); // memory region already exists
  assert(munmap(addr, pagesize) == 0);
  assert(munmap(addr2, pagesize) == 0);

  addr2 = mmap_fixed_noreplace(addr, pagesize, 0, test_flag, -1, 0);
  assert(addr2 != MAP_FAILED && addr2 == addr);
  assert(munmap(addr, pagesize) == 0);

#if MAP_FIXED_NOREPLACE != -1
  addr2 = mmap_fixed_noreplace(addr, pagesize, 0,
                               test_flag | MAP_FIXED_NOREPLACE, -1, 0);
  assert(addr2 != MAP_FAILED && addr2 == addr);
  addr2 = mmap_fixed_noreplace(addr, pagesize, 0,
                               test_flag | MAP_FIXED_NOREPLACE, -1, 0);
  assert(addr2 == MAP_FAILED && errno == EEXIST);
  assert(munmap(addr, pagesize) == 0);
#endif

  return 0;
}
#endif
