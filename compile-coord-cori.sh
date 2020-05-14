#!/bin/bash
cd src/
CC -DHAVE_CONFIG_H -I. -I../include  -I../include -I../jalib -Wall -DDEBUG -fPIC \
   -std=c++11 -g3 -O0 -DDEBUG -MT dmtcp_coordinator.o -MD -MP -MF .deps/dmtcp_coordinator.Tpo \
   -c -o dmtcp_coordinator.o dmtcp_coordinator.cpp
# Next line no longer needed.  'make -j' does it.
# g++ -g3 -O0 -o ../bin/dmtcp_coordinator dmtcp_coordinator.o lookup_service.o restartscript.o \
#       	 libdmtcpinternal.a libjalib.a libnohijack.a -lpthread -lrt
