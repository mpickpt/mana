# MPI compiler
MPICC = mpicc
MPICXX = mpic++

TESTS = record-replay-comm-test \
        record-replay-group-test \
        record-replay-types-test \
        record-replay-cart-test \
        drain-send-recv-test

TEST_OBJS=$(addsuffix .o, ${TESTS})
TEST_BINS=$(addsuffix .exe, ${TESTS})

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I.. -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I.. -I${JALIB_INCLUDE} \
                     -I${DMTCP_ROOT}/src -std=c++11 \
                     -D'NEXT_FUNC(fnc)=MPI_\#\#fnc'

TEST_LD_FLAGS=${DMTCP_ROOT}/src/libjalib.a \
              ${DMTCP_ROOT}/src/libdmtcpinternal.a \
              ${DMTCP_ROOT}/src/libnohijack.a \
              -lcppunit -lpthread

DRAIN_TEST_OBJS = drain-send-recv-test.o ../drain_send_recv_packets.cpp \
             ${DMTCP_ROOT}/src/lookup_service.o

default: ${TEST_BINS}

../record-replay.o ../drain_send_recv_packets.o:
	@make -C ..

%.exe: %.o ../record-replay.o
	${MPICXX} -fPIC -g3 -O0 -o $@ $^ ${TEST_LD_FLAGS}

drain-send-recv-test.exe: ${DRAIN_TEST_OBJS}
	${MPICXX} -fPIC -g3 -O0 ${CXXFLAGS} -o $@ $^ ${TEST_LD_FLAGS}

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

gdb-%: ${TEST_BINS}
	gdb --args ./record-replay-$*-test.exe

check: ${TEST_BINS}
	@for x in $^; do mpirun -n 1 ./$$x; done

clean: tidy
	rm -f ${TEST_BINS} ${TEST_OBJS}

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean vi vim touch gdb tidy check
