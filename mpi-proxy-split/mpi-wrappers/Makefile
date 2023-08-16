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
                  mpi_wrappers.o get_fortran_constants.o fortran_constants.o \
                  mpi_win_wrappers.o mpi_file_wrappers.o mpi_error_wrappers.o \


# Modify if your MANA_ROOT is located elsewhere.
ifndef MANA_ROOT
  MANA_ROOT=../..
endif
# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=${MANA_ROOT}/dmtcp
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

# This inherits the CFLAGS/CXXFLAGS from './configure' (w/ optional --enable-debug)..
override CFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${PLUGIN_HALF_SRCDIR} \
	                 -I${LOWER_HALF_SRCDIR}
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${PLUGIN_HALF_SRCDIR} -I${DMTCP_ROOT}/src \
                     -I${LOWER_HALF_SRCDIR}

# We don't build libmpiwrappers.a.  Instead, we use *.o to build libmana.so.
default: ${LIBNAME}.a libmpistub.so

# If any .o file is re-built, then re-build mpi_collective_wrappers.o .
# That recipe will test the environment variable: DEFINE_mpi_collective_p2p
# If the env. var. is set, then the Makefile will cause
#   mpi_collective_wrappers.cpp to include mpi_collective_p2p.c .
${LIBNAME}.a: ${LIBWRAPPER_OBJS}
	touch mpi_collective_wrappers.cpp
	${MAKE} mpi_collective_wrappers.o
	ar cr $@ $^

mpi_unimplemented_wrappers.cpp: generate-mpi-unimplemented-wrappers.py \
	mpi_unimplemented_wrappers.txt
	python3 $^ > $@

.c.o:
	${MPICC} ${CFLAGS} -c -o $@ $<

.cpp.o: ../virtual-ids.h
	${MPICXX} ${CXXFLAGS} -c -o $@ $<

p2p-deterministic.h: p2p-deterministic.txt p2p-deterministic.py
	./p2p-deterministic.py $< > $@
mpi_p2p_wrappers.o: mpi_p2p_wrappers.cpp p2p-deterministic.h
	${MPICXX} ${CXXFLAGS} -c -o $@ $<
mpi_request_wrappers.o: mpi_request_wrappers.cpp p2p-deterministic.h
	${MPICXX} ${CXXFLAGS} -c -o $@ $<

# FIXME:  This assumes 'CFLAGS' is compatible with FORTRAN, since MANA does not
#         define 'FFLAGS'.  But Fortran doesn't use '-std=gnu11' from CFLAGS.
fortran_constants.o: fortran_constants.f90
	${MPIFORTRAN} ${CFLAGS} -fPIC -c -o $@ $<

mpi_collective_wrappers.o: mpi_collective_wrappers.cpp ../virtual-ids.h \
                           mpi_collective_p2p.c
	if [ ! -z "$$MPI_COLLECTIVE_P2P" ]; then \
	  DEFINE_mpi_collective_p2p="-DMPI_COLLECTIVE_P2P"; \
	else \
	  DEFINE_mpi_collective_p2p=""; \
	fi && \
	${MPICXX} ${CXXFLAGS} -Wall -DDEBUG -fPIC $$DEFINE_mpi_collective_p2p \
          -I.. -I../lower-half -c $<

# Some of these functions are also collective; e.g., MPI_Win_allocate_shared
mpi_win_wrappers.o: mpi_win_wrappers.cpp ../virtual-ids.h
	${MPICXX} ${CXXFLAGS} -Wall -DDEBUG -fPIC \
          -I.. -I../lower-half -c $<

# We need to write to mpi_stub_wrappers.c.tmp.$$ first, in case of 'make -j'.
# The parallel make would generate mpi_stub_wrappers.c 3 times, and overwrite.
mpi_stub_wrappers.c: generate-mpi-stub-wrappers.py mpi_stub_wrappers.txt
	rm -f $@
	tmp=$@.tmp.$$$$ ; \
	printf "%s\n\n" \
	 "// *** THIS FILE IS AUTO-GENERATED! DO 'make' TO UPDATE. ***" >$$tmp;\
	python3 $^ >> $$tmp && mv -f $$tmp $@ || (rm -f $$tmp && false)

mpi_fortran_wrappers.cpp: generate-mpi-fortran-wrappers.py \
                          mpi_fortran_wrappers.txt
	rm -f $@
	tmp=$@.tmp.$$$$ ; \
	printf "%s\n\n" \
	 "// *** THIS FILE IS AUTO-GENERATED! DO 'make' TO UPDATE. ***" >$$tmp;\
	python3 $^ >> $$tmp && mv -f $$tmp $@ || (rm -f $$tmp && false)

mpi_stub_wrappers.o: mpi_stub_wrappers.c
	${MPICC} ${CFLAGS} -c -o $@ $<

libmpistub.so: mpi_stub_wrappers.o
	${CC} -fPIC $< -shared -o $@

libmpich_gnu_82.so.3.0.1: libmpistub.so
	ln -sf $< $@

libmpich_gnu_82.so.3: libmpich_gnu.so.3.0.1
	ln -sf $< $@

libmpich_intel.so.3.0.1: libmpistub.so
	ln -sf $< $@

libmpich_intel.so.3: libmpich_intel.so.3.0.1
	ln -sf $< $@

libpmi.so.0.5.0: libmpistub.so
	ln -sf $< $@

libpmi.so.0: libpmi.so.0.5.0
	ln -sf $< $@

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

install: libmpistub.so 
	cp -f libmpistub.so ${MANA_ROOT}/lib/dmtcp/
	cd ${MANA_ROOT}/lib/dmtcp && \
		ln -sf libmpistub.so libmpich_intel.so.3.0.1 && \
		ln -sf libmpich_intel.so.3.0.1 libmpich_intel.so.3 && \
		ln -sf libmpistub.so libmpich_gnu_82.so.3.0.1 && \
		ln -sf libmpich_gnu_82.so.3.0.1 libmpich_gnu_82.so.3
	cd ${MANA_ROOT}/lib/dmtcp && \
		ln -sf libmpistub.so libpmi.so.0.5.0 && \
		ln -sf libpmi.so.0.5.0 libpmi.so.0

clean: tidy
	rm -f ${LIBWRAPPER_OBJS}
	rm -f ${LIBNAME}.a libmpistub.so
	rm -f ${MANA_ROOT}/lib/dmtcp/libmpistub.so
	rm -f mpi_stub_wrappers.c
	rm -f p2p-deterministic.h
	rm -f mpi_unimplemented_wrappers.cpp
	rm -f mpi_fortran_wrappers.cpp
	rm -f libmpich_intel.so.3.0.1 libmpich_intel.so.3
	rm -f libmpich_gnu_82.so.3.0.1 libmpich_gnu_82.so.3
	rm -f libpmi.so.3.0.1 libpmi.so.0
	if test -d ${MANA_ROOT}/lib/dmtcp; then \
        cd ${MANA_ROOT}/lib/dmtcp && rm -f libmpich_gnu_82.so.* \
            libmpich_intel.so.* libpmi.so.*; \
    fi


distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean tidy install
