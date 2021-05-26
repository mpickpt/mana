# MPI compiler according to the platform
PLATFORM=${shell echo $$HOST}
include Makefile_config

# These are needed for libmana.so, since the wrapper functions there will
# call the lower half.  That requires temporarily changing the FS register,
# and apparently, glibc detects this as an attempt to do stack smashing.
CFLAGS=-fno-stack-protector
CXXFLAGS=-fno-stack-protector

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting plugin library will have this name.
LIBNAME=libmana

LOWER_HALF_SRCDIR=lower-half

# As you add new files to your plugin library, add the object file names here.

WRAPPERS_SRCDIR=mpi-wrappers
LIBWRAPPER=libmpiwrappers.a

LIBOBJS = mpi_plugin.o drain_send_recv_packets.o \
          record-replay.o two-phase-algo.o \
          split_process.o ${LOWER_HALF_SRCDIR}/procmapsutils.o

MANA_COORD_OBJS = mana_coordinator.o

PROXY_BIN = lh_proxy
LIBPROXY = libproxy.a

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${LOWER_HALF_SRCDIR} -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${WRAPPERS_SRCDIR} -I. \
                     -I${DMTCP_ROOT}/src -I${LOWER_HALF_SRCDIR} -std=c++11

default: ${LIBNAME}.so ${MANA_COORD_OBJS}
	@make -C ${LOWER_HALF_SRCDIR} ${PROXY_BIN}
	@make -C ${WRAPPERS_SRCDIR} libmpidummy.so
	@make -C ${WRAPPERS_SRCDIR} libmpich_intel.so.3

${LIBNAME}.so: ${LIBOBJS}
	@make -C ${WRAPPERS_SRCDIR} ${LIBWRAPPER}
	${CXX} -shared -fPIC -g3 -O0 -o $@ ${LIBOBJS} -Wl,--whole-archive ${WRAPPERS_SRCDIR}/${LIBWRAPPER} -Wl,--no-whole-archive

mpi_unimplemented_wrappers.cpp: generate-mpi-unimplemented-wrappers.py mpi_unimplemented_wrappers.txt
	python $^ > $@

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

${LOWER_HALF_SRCDIR}/procmapsutils.o: ${LOWER_HALF_SRCDIR}/procmapsutils.c
	@make -C ${LOWER_HALF_SRCDIR} procmapsutils.o

vi vim: ${FILE}
	vim $<

touch: ${FILE}
	$@ $<

gdb: ${basename lh_proxy.c ${FILE}}
	$@ $<

tests:
	@make -C test/ default

check: ${LIBNAME}.so ${LOWER_HALF_SRCDIR}/${PROXY_BIN} ./autotest.py
	@make -C test/ default
	@python ./autotest.py

check-unit: ${LIBNAME}.so
	@make -C unit-test/ check

install: ${LIBNAME}.so
	@make -C ${LOWER_HALF_SRCDIR} ${PROXY_BIN}
	@make -C ${LOWER_HALF_SRCDIR} install
	@make -C ${WRAPPERS_SRCDIR} libmpich_intel.so.3
	@make -C ${WRAPPERS_SRCDIR} libpmi.so.0
	cp ${LIBNAME}.so ${DMTCP_ROOT}/lib/dmtcp
	@cd ${WRAPPERS_SRCDIR} && make install

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

clean: tidy
	@cd ${LOWER_HALF_SRCDIR} && make clean
	rm -f ${LIBOBJS} ${MANA_COORD_OBJS}
	rm -f ${LIBNAME}.so
	@cd ${WRAPPERS_SRCDIR} && make clean

distclean: clean
	cd test && make clean
	cd unit-test && make clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean vi vim touch gdb tidy check \
        tests check-unit install
