#ifndef MEMORY_ACCESS_TRACKER
#define MEMORY_ACCESS_TRACKER

#include <stdio.h>
#include <stdint.h>

typedef struct MemTracker {
  /* TODO: support multiple trackers */
  // struct list_head node;
  void *addr;
  size_t len;
  int fd; // for debug
  uint32_t max_num; // max number of access will be tracked
  /* If there're too many accesses to the same address,
   * May change to use map<addr, count> instead here */
  uint32_t num_access; // number of access happened
  void** addrs; // addresses have been accessed
} MemTracker_t;

struct MemTracker* StartTrackMemory(void *addr, size_t len, uint32_t max_num, int fd);
void EndTrackMemory(struct MemTracker *tracker);
void FreeMemTracker(struct MemTracker *tracker);

#endif
