
all: lib test

DEFINES := -D_GNU_SOURCE
CFLAGS := -Wall

DEBUG ?= 1
ifeq ($(DEBUG), 1)
  DEFINES += -DDEBUG
  CFLAGS += -O0 -ggdb3
else
  DEFINES += -DNDEBUG
  CFLAGS += -O2 -g
endif

memory_access_tracker.o: memory_access_tracker.c memory_access_tracker.h
	gcc $(DEFINES) $(CFLAGS) -fPIC -c -o $@ $<

libmemtracker.so: memory_access_tracker.o
	gcc -o $@ -shared $^

libmemtracker.a: memory_access_tracker.o
	ar rcs $@ $^

lib: libmemtracker.so libmemtracker.a

test_memory_tracker: test_memory_tracker.c libmemtracker.a
	gcc $(DEFINES) $(CFLAGS) -o $@ $^
  
test: test_memory_tracker

clean:
	rm *.o libmemtracker.so libmemtracker.a test_memory_tracker
