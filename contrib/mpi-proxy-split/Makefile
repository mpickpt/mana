# MPI compiler according to the platform
PLATFORM=${shell echo $$HOST}
ifeq ($(findstring cori,$(PLATFORM)),cori)
 MPICC = cc
 MPICXX = CC
 MPI_LD_FLAG = -lmpich
else
 MPICC = mpicc
 MPICXX = mpic++
 MPI_LD_FLAG = -lmpi
endif

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting plugin library will have this name.
LIBNAME=libdmtcp_${NAME}

LOWER_HALF_SRCDIR=lower-half

# As you add new files to your plugin library, add the object file names here.
LIBOBJS = mpi_plugin.o mpi_wrappers.o drain_send_recv_packets.o \
          mpi_p2p_wrappers.o mpi_collective_wrappers.o mpi_request_wrappers.o \
          mpi_type_wrappers.o mpi_comm_wrappers.o mpi_group_wrappers.o \
          mpi_cart_wrappers.o mpi_op_wrappers.o \
          mpi_unimplemented_wrappers.o \
          mpi_fortran_wrappers.o \
          record-replay.o \
          two-phase-algo.o \
          split_process.o ${LOWER_HALF_SRCDIR}/procmapsutils.o

PROXY_BIN = proxy
LIBPROXY = libproxy.a

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${LOWER_HALF_SRCDIR} -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${DMTCP_ROOT}/src -I${LOWER_HALF_SRCDIR} -std=c++11

default: ${LIBNAME}.so ${LOWER_HALF_SRCDIR}/${PROXY_BIN} libmpidummy.so libmpich_intel.so.3

${LIBNAME}.so: ${LIBOBJS}
	${MPICXX} -shared -fPIC -g3 -O0 -o $@ $^

mpi_unimplemented_wrappers.cpp: generate-mpi-unimplemented-wrappers.py mpi_unimplemented_wrappers.txt
	python $^ > $@

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

${LOWER_HALF_SRCDIR}/procmapsutils.o:
	@make -C ${LOWER_HALF_SRCDIR} procmapsutils.o

${LOWER_HALF_SRCDIR}/${PROXY_BIN}: ${LOWER_HALF_SRCDIR}/${LIBPROXY}
	@make -C ${LOWER_HALF_SRCDIR} ${PROXY_BIN}

${LOWER_HALF_SRCDIR}/${LIBPROXY}:
	@make -C ${LOWER_HALF_SRCDIR} ${LIBPROXY}

mpi_dummy.c: generate-mpi-dummy-wrappers.py mpi_dummy_wrappers.txt
	python $^ > $@

mpi_fortran_wrappers.cpp: generate-mpi-fortran-wrappers.py mpi_fortran_wrappers.txt
	python $^ > $@

libmpidummy.so: mpi_dummy.c
	${MPICC} -fPIC -g3 -O0 $< -shared -o $@

libmpich_intel.so.3.0.1: libmpidummy.so
	cp -f $< $@

libmpich_intel.so.3: libmpich_intel.so.3.0.1
	ln -sf $< $@

vi vim: ${FILE}
	vim $<

touch: ${FILE}
	$@ $<

gdb: ${basename proxy.c ${FILE}}
	$@ $<

tests:
	@make -C test/

check: ${LIBNAME}.so ${LOWER_HALF_SRCDIR}/${PROXY_BIN} ./autotest.py
	@make -C test/
	@python ./autotest.py

check-unit: ${LIBNAME}.so
	@make -C unit-test/ check

install: ${LOWER_HALF_SRCDIR}/${PROXY_BIN} ${LIBNAME}.so libmpich_intel.so.3
	@make -C ${LOWER_HALF_SRCDIR} install
	cp ${LIBNAME}.so ${DMTCP_ROOT}/lib/dmtcp
	cp libmpidummy.so ${DMTCP_ROOT}/lib/dmtcp
	cp -a libmpich_intel.so.3 ${DMTCP_ROOT}/lib/dmtcp
	cp libmpich_intel.so.3.0.1 ${DMTCP_ROOT}/lib/dmtcp

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

clean: tidy
	@make -C ${LOWER_HALF_SRCDIR} clean
	rm -f ${LIBOBJS}
	rm -f ${LIBNAME}.so libmpidummy.so
	rm -f mpi_unimplemented_wrappers.cpp
	rm -f libmpich_intel.so.3.0.1 libmpich_intel.so.3

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean vi vim touch gdb tidy check \
        tests check-unit install
