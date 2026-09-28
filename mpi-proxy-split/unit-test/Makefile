# MPI compiler
include ../Makefile_config

ifeq (${PERLMUTTER}, 1)
MPIRUN = srun
MPICC = cc
MPICXX = CC
else
MPIRUN = mpirun
MPICC = mpicc
MPICXX = mpic++
endif

TESTS = record-replay-comm-test \
        record-replay-group-test \
        record-replay-types-test

TEST_OBJS=$(addsuffix .o, ${TESTS})
TEST_BINS=$(addsuffix .exe, ${TESTS})
# Tests that don't need gtest.
PLAIN_TEST_BINS=virtual-id-test.exe

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../dmtcp
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib
LOWER_HALF_INCLUDE=../lower-half
MPI_WRAPPERS_INCLUDE=../mpi-wrappers

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I..
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} \
                     -I.. -I${JALIB_INCLUDE} \
                     -I${LOWER_HALF_INCLUDE} \
                     -I${MPI_WRAPPERS_INCLUDE} \
                     -I${DMTCP_ROOT}/src \
                     -D'NEXT_FUNC(fnc)=MPI_\#\#fnc'

TEST_LD_FLAGS=${DMTCP_ROOT}/src/libdmtcpinternal.a \
              ${DMTCP_ROOT}/src/libjalib.a \
              ${DMTCP_ROOT}/src/libnohijack.a \
              -lgtest -lpthread -ldl

DRAIN_TEST_OBJS = drain-send-recv-test.o ../p2p_drain_send_recv.cpp \
                  ../p2p_log_replay.cpp ${DMTCP_ROOT}/src/lookup_service.o

default: ${TEST_BINS} ${PLAIN_TEST_BINS}

../record-replay.o ../drain_send_recv_packets.o:
	@make -C ..

# NOTE:  The objects files split_process.o and procmapsutils.o
# below are required only when MANA is configured with SINGLE_CART_REORDER
# C/C++ flag. One should remove these two obj files when we decide to remove
# the SINGLE_CART_REORDER macro implementation entirely from MANA.
%.exe: %.o ../record-replay.o  ../split_process.o ../lower-half/procmapsutils.o
	${MPICXX} -fPIC -g3 -O0 -o $@ $^ ${TEST_LD_FLAGS}

drain-send-recv-test.exe: ${DRAIN_TEST_OBJS}
	${MPICXX} -fPIC -g3 -O0 ${CXXFLAGS} -o $@ $^ ${TEST_LD_FLAGS}

# The lower half's predefined constants, for tests that stand in for it.
lh_constants.inc: ../lower-half/lower-half.cpp
	sed -n '/lh_info->MANA_GROUP_NULL = /,/lh_info->MANA_ERRORS_RETURN = /p' \
	  $< > $@

# Built with -O2: 'virtual-id-test.exe --bench' times the table.
virtual-id-test.exe: virtual-id-test.cpp lh_constants.inc ../virtual_id.cpp \
                     ../virtual_id.h ../lower-half/switch-context.cpp
	${MPICXX} ${CXXFLAGS} -O2 -o $@ virtual-id-test.cpp ../virtual_id.cpp \
	  ../lower-half/switch-context.cpp

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

gdb-%: ${TEST_BINS}
	gdb --args ./record-replay-$*-test.exe

check: ${TEST_BINS} ${PLAIN_TEST_BINS}
	@for x in $^; do ${MPIRUN} -n 1 ./$$x; done

clean: tidy
	rm -f ${TEST_BINS} ${PLAIN_TEST_BINS} ${TEST_OBJS} lh_constants.inc

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean vi vim touch gdb tidy check
