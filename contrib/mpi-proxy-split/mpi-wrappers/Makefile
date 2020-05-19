# MPI compiler according to the platform
PLATFORM=${shell echo $$HOST}
include ../Makefile_config

WRAPPERS_SRCDIR = ${PWD}

# By default, your resulting plugin library will have this name.
LIBNAME=libmpiwrappers

LOWER_HALF_SRCDIR=../lower-half
PLUGIN_HALF_SRCDIR=..

# As you add new files to your plugin library, add the object file names here.
LIBWRAPPER_OBJS = mpi_p2p_wrappers.o mpi_collective_wrappers.o \
                  mpi_request_wrappers.o mpi_type_wrappers.o \
                  mpi_comm_wrappers.o mpi_group_wrappers.o \
                  mpi_cart_wrappers.o mpi_op_wrappers.o \
                  mpi_unimplemented_wrappers.o mpi_fortran_wrappers.o \
                  mpi_wrappers.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${PLUGIN_HALF_SRCDIR} \
	                 -I${LOWER_HALF_SRCDIR} -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${PLUGIN_HALF_SRCDIR} -I${DMTCP_ROOT}/src \
										 -I${LOWER_HALF_SRCDIR} -std=c++11

# We don't build libmpiwrappers.a.  Instead, we use *.o to build libmana.so.
default: ${LIBNAME}.a libmpidummy.so libmpich_intel.so.3

${LIBNAME}.a: ${LIBWRAPPER_OBJS}
	ar cr $@ $^

mpi_unimplemented_wrappers.cpp: generate-mpi-unimplemented-wrappers.py mpi_unimplemented_wrappers.txt
	python $^ > $@

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

mpi_dummy.c: generate-mpi-dummy-wrappers.py mpi_dummy_wrappers.txt
	python $^ > $@

mpi_fortran_wrappers.cpp: generate-mpi-fortran-wrappers.py mpi_fortran_wrappers.txt
	python $^ > $@

mpidummy.o: mpidummy.c
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

libmpidummy.so: mpi_dummy.o
	${CC} -fPIC -g3 -O0 $< -shared -o $@

libmpich_intel.so.3.0.1: libmpidummy.so
	cp -f $< $@

libmpich_intel.so.3: libmpich_intel.so.3.0.1
	ln -sf $< $@

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

install: ${WRAPPERS_SRCDIR}/libmpich_intel.so.3 
	cp libmpidummy.so ${DMTCP_ROOT}/lib/dmtcp
	cp -a libmpich_intel.so.3 ${DMTCP_ROOT}/lib/dmtcp
	cp libmpich_intel.so.3.0.1 ${DMTCP_ROOT}/lib/dmtcp

clean: tidy
	rm -f ${LIBWRAPPER_OBJS}
	rm -f ${LIBNAME}.a libmpidummy.so
	rm -f mpi_unimplemented_wrappers.cpp
	rm -f libmpich_intel.so.3.0.1 libmpich_intel.so.3

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean tidy install
