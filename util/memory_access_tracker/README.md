# Memory Access Tracker

Linux only provides the way to track memory access in page level, like soft dirty bits and userfault. This tool can track memory access in a more granuale level.

# APIs

`StartTrackMemory` starts track memory start from `addr`(has to be page aligned) with size `len`. User also needs to specify the `max_num` which is the maxinum number of access it trackes and `fd` which it can write some logs.

**Warning** The performance will be dramatically downgraded.

`EndTrackMemory` Stops tracking the memory access, after this, user can get the number of accesses(`num_access`) and the accessed memory addresses(`addrs`) from the tracker after this.

`FreeMemTracker` frees the memory used by the data structure.

# Sample useage

You can check the program example in `test_memory_tracker.c`

```
$ make
gcc -D_GNU_SOURCE -DDEBUG -Wall -O0 -ggdb3 -fPIC -c -o memory_access_tracker.o memory_access_tracker.c
gcc -o libmemtracker.so -shared memory_access_tracker.o
ar rcs libmemtracker.a memory_access_tracker.o
gcc -D_GNU_SOURCE -DDEBUG -Wall -O0 -ggdb3 -o test_memory_tracker test_memory_tracker.c libmemtracker.a
$ ./test_memory_tracker
main 35 Test 0x7f32a1e06000
main 40 Read 0x7f32a1e06000: 0
main 46 Read 0x7f32a1e06000: 1
main 49 Read 0x7f32a1e06400: 0
main 53 access 39 times
0x7f32a1e06000
0x7f32a1e06000
0x7f32a1e06000
0x7f32a1e06064
0x7f32a1e06800
0x7f32a1e06bf0
0x7f32a1e06be0
0x7f32a1e06bd0
0x7f32a1e06bc0
0x7f32a1e06840
0x7f32a1e06860
0x7f32a1e06880
0x7f32a1e068a0
0x7f32a1e068c0
0x7f32a1e068e0
0x7f32a1e06900
0x7f32a1e06920
0x7f32a1e06940
0x7f32a1e06960
0x7f32a1e06980
0x7f32a1e069a0
0x7f32a1e069c0
0x7f32a1e069e0
0x7f32a1e06a00
0x7f32a1e06a20
0x7f32a1e06a40
0x7f32a1e06a60
0x7f32a1e06a80
0x7f32a1e06aa0
0x7f32a1e06ac0
0x7f32a1e06ae0
0x7f32a1e06b00
0x7f32a1e06b20
0x7f32a1e06b40
0x7f32a1e06b60
0x7f32a1e06b80
0x7f32a1e06ba0
0x7f32a1e06bc0
0x7f32a1e06be0
```

# Reference

https://programmer.group/x86_-principle-and-example-of-single-step-debugging-for-64-platform.html
