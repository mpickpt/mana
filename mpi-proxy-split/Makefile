include Makefile_config

# These are needed for libmana.so, since the wrapper functions there will
# call the lower half.  That requires temporarily changing the FS register,
# and apparently, glibc detects this as an attempt to do stack smashing.
override CFLAGS += -fno-stack-protector
override CXXFLAGS += -fno-stack-protector

# Targets to install:
#   libmana.so (using mpi-wrappers/libmpiwrappers.a), libmpistub.so,
#   libmpistub_audit.so

# Subdirectories needed to install
LOWER_HALF_SRCDIR=lower-half
WRAPPERS_SRCDIR=mpi-wrappers

# As you add new files to your plugin library, add the object file names here.

LIBOBJS = uh_wrappers.o mpi_plugin.o p2p_drain_send_recv.o p2p_log_replay.o \
          seq_num.o virtual_id.o lower_half_ckpt.o \
          lower-half/switch-context.o \

# Modify if your MANA_ROOT is located elsewhere.
ifndef MANA_ROOT
  MANA_ROOT=..
endif
# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=${MANA_ROOT}/dmtcp
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${LOWER_HALF_SRCDIR}
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${WRAPPERS_SRCDIR} -I. \
                     -I${DMTCP_ROOT}/src -I${LOWER_HALF_SRCDIR}
# The upper half has no MPI library, so leave out the C++ bindings that
# mpi.h declares in C++ (Open MPI 4.1: MPI::Datatype::Free(), ...), whose
# code is in libmpi_cxx.so.
override CXXFLAGS += -DOMPI_SKIP_MPICXX -DMPICH_SKIP_MPICXX

# ${WRAPPERS_SRCDIR}/libmpiwrappers.a ia a prerequisite for libmana.so
# Always do 'make default' or 'make install' -- not 'make libmana.so'
default:
	make -C ${LOWER_HALF_SRCDIR} lower-half
	make -C ${WRAPPERS_SRCDIR} libmpiwrappers.a libmpistub.so \
	  libmpistub_audit.so
	make libmana.so

# ${WRAPPERS_SRCDIR}/libmpiwrappers.a made in install/default before libmana.so
# libmana.so needs libmpistub.so (in the same directory once installed): with
# Open MPI, it refers to the MPI library's objects (see
# generate-mpi-stub-objects.py), and a program that loads the MPI library
# with dlopen() has not loaded libmpistub.so yet.
libmana.so: ${LIBOBJS} ${WRAPPERS_SRCDIR}/libmpiwrappers.a
	${CXX} -shared -fPIC -g3 -O0 -o $@ ${LIBOBJS} -Wl,--whole-archive ${WRAPPERS_SRCDIR}/libmpiwrappers.a -Wl,--no-whole-archive \
	  -L${WRAPPERS_SRCDIR} -lmpistub -Wl,-rpath,'$$ORIGIN'

.c.o:
	${MPICC} ${CFLAGS} -c -o $@ $<

.cpp.o:
	${MPICXX} ${CXXFLAGS} -c -o $@ $<

# Make ignores prerequisites on suffix rules, so list header dependencies
# here: the headers that define inline code.
$(filter-out lower-half/%,${LIBOBJS}): virtual_id.h lower_half_ckpt.h

tests:
	+ @make -C test/ default
	+ @make -C examples/

check: libmana.so
	@make -C test/ e2e
	@test/autotest.py

${MANA_ROOT}/lib/dmtcp/libmana.so: libmana.so
	cp -f $< $@
${MANA_ROOT}/lib/dmtcp/libmpistub.so: ${WRAPPERS_SRCDIR}/mpi_stub_wrappers.txt \
                                      ${WRAPPERS_SRCDIR}/mpi_stub_data.c
	+ make -C ${WRAPPERS_SRCDIR} install
${MANA_ROOT}/lib/dmtcp/libmpistub_audit.so: ${WRAPPERS_SRCDIR}/mpi_stub_audit.c
	+ make -C ${WRAPPERS_SRCDIR} install

# ${WRAPPERS_SRCDIR}/libmpiwrappers.a ia a prerequisite for libmana.so,
#   which is a prerequisite for ${MANA_ROOT}/lib/dmtcp/libmana.so
# Always do 'make default' or 'make install' -- not 'make libmana.so'
install:
	+ make -C ${WRAPPERS_SRCDIR} libmpiwrappers.a libmpistub.so
	+ make ${MANA_ROOT}/lib/dmtcp/libmana.so
	+ make ${MANA_ROOT}/lib/dmtcp/libmpistub.so
	+ make ${MANA_ROOT}/lib/dmtcp/libmpistub_audit.so
	+ make -C ${LOWER_HALF_SRCDIR} install

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*
	rm -rf dmtcp_coordinator_db-*.json

clean: tidy
	rm -f ${LIBOBJS}
	rm -f libmana.so
	rm -f ${MANA_ROOT}/lib/dmtcp/libmana.so
	cd ${LOWER_HALF_SRCDIR} && make clean
	cd ${WRAPPERS_SRCDIR} && make clean

distclean: clean

.PHONY: default clean distclean tidy check tests install
