// mmap() from several threads at once.  Under MANA the upper half's mmap()
// and munmap() go to the lower half's wrapper, which picks addresses from its
// own lists.  Each of 4 threads maps 4 KiB to 512 KiB, records the range in a
// table of live ranges (and fails if it overlaps one), fills it with its own
// pattern, checks the pattern, and unmaps it.  The main thread reports
// progress, and checkpoints come while the threads run.  It calls
// MPI_Barrier in each iteration, so that MPI runs in the lower half while the
// threads are in the wrapper; with -q (mmap_threads_quiet) it does not.

#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include "mana_test.h"

#define NTHREADS 4
#define MAXLIVE 64

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t live_lo[MAXLIVE], live_hi[MAXLIVE];
static volatile int stop;
static volatile long rounds[NTHREADS];

static void
add_live(int slot, char *p, size_t len)
{
  pthread_mutex_lock(&lock);
  for (int i = 0; i < MAXLIVE; i++) {
    MT_CHECK(live_hi[i] <= (uintptr_t)p || live_lo[i] >= (uintptr_t)p + len,
             "mmap(NULL, %#zx) = %p overlaps a live mapping %#lx-%#lx", len,
             (void *)p, (unsigned long)live_lo[i], (unsigned long)live_hi[i]);
  }
  live_lo[slot] = (uintptr_t)p;
  live_hi[slot] = (uintptr_t)p + len;
  pthread_mutex_unlock(&lock);
}

static void
remove_live(int slot)
{
  pthread_mutex_lock(&lock);
  live_lo[slot] = live_hi[slot] = 0;
  pthread_mutex_unlock(&lock);
}

static void *
worker(void *arg)
{
  int t = (int)(intptr_t)arg;
  uint64_t seed = 0x9e3779b97f4a7c15ULL * (t + 1);
  for (long r = 0; !stop; r++) {
    // Up to MAXLIVE / NTHREADS live mappings per thread.
    char *p[MAXLIVE / NTHREADS];
    size_t len[MAXLIVE / NTHREADS];
    int n = 1 + (int)(r % (MAXLIVE / NTHREADS));
    for (int i = 0; i < n; i++) {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      len[i] = (size_t)4096 << (seed >> 61);  // 4 KiB to 512 KiB
      p[i] = mmap(NULL, len[i], PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      MT_CHECK(p[i] != MAP_FAILED, "thread %d: mmap of %zu bytes failed", t,
               len[i]);
      add_live(t * (MAXLIVE / NTHREADS) + i, p[i], len[i]);
      for (size_t k = 0; k < len[i]; k += 4096) {
        p[i][k] = (char)(t * 31 + i + k / 4096);
      }
    }
    for (int i = 0; i < n; i++) {
      for (size_t k = 0; k < len[i]; k += 4096) {
        MT_CHECK(p[i][k] == (char)(t * 31 + i + k / 4096),
                 "thread %d: page %zu of %p (%#zx bytes) changed", t,
                 k / 4096, (void *)p[i], len[i]);
      }
      remove_live(t * (MAXLIVE / NTHREADS) + i);
      MT_CHECK(munmap(p[i], len[i]) == 0, "thread %d: munmap failed", t);
    }
    rounds[t] = r;
  }
  return NULL;
}

int
main(int argc, char **argv)
{
  int quiet = argc > 1 && strcmp(argv[1], "-q") == 0;
  mt_init(&argc, &argv, quiet ? "mmap_threads_quiet" : "mmap_threads");
  pthread_t th[NTHREADS];
  for (int t = 0; t < NTHREADS; t++) {
    MT_CHECK(pthread_create(&th[t], NULL, worker, (void *)(intptr_t)t) == 0,
             "pthread_create");
  }
  long it;
  for (it = 0; mt_continue(it); it++) {
    usleep(10000);
    if (!quiet) {
      MT_MPI(MPI_Barrier(MPI_COMM_WORLD));
    }
  }
  stop = 1;
  for (int t = 0; t < NTHREADS; t++) {
    pthread_join(th[t], NULL);
  }
  mt_finish(it);
  return 0;
}
