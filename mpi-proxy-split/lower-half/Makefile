# MPI compiler according to the platform
UNAME_RELEASE=${shell uname -r}
include ../Makefile_config

# Modify if your MANA_ROOT is located elsewhere.
ifndef MANA_ROOT
  MANA_ROOT=../..
endif
# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=${MANA_ROOT}/dmtcp
endif

ifndef PLUGIN_ROOT
  PLUGIN_ROOT=..
endif

DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib
MANA_INCLUDE=${MANA_ROOT}/mpi-proxy-split
PLUGIN_INCLUDE=${PLUGIN_ROOT}

DMTCP_DEPS = ${DMTCP_ROOT}/src/libdmtcprestart.a \
	     ${DMTCP_ROOT}/src/libdmtcpinternal.a \
	     ${DMTCP_ROOT}/src/libjalib.a \
	     ${DMTCP_ROOT}/src/libnohijack.a
	     # ${DMTCP_ROOT}/src/mtcp/libmtcprestart.a

override CFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${MANA_INCLUDE} -g3 -O0 \
                   -I${DMTCP_ROOT}/src 
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -I${MANA_INCLUDE} -I${DMTCP_ROOT}/src/mtcp \
                     -I${DMTCP_ROOT}/src

TEXTSEG_ADDR_FLAG=-Wl,-Ttext-segment=0x10000000

default: kernel-loader

.cpp.o:
	${MPICXX} ${CXXFLAGS} -c -o $@ $<

.c.o:
	${MPICXX} ${CFLAGS} -c -o $@ $<

# Normally, gcc (or clang) will automatically link with libatomic.so.
# However, for come MPICXX it's hard to know if they cover it.
# So this is good as a safety measure.
kernel-loader: get_symbol_offset.o copy_stack.o patch_trampoline.o lh_func_ptr.o mem_wrapper.o switch_context.o kernel_loader.o mmap-fixed-noreplace.o ${DMTCP_DEPS}
	${MPICXX} -g3 ${TEXTSEG_ADDR_FLAG} ${CXXFLAGS} -o $@ $^ -lpthread -ldl -latomic

install: kernel-loader
	cp -f $^ ${MANA_ROOT}/bin/

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*
	rm -rf dmtcp_coordinator_db-*.json

clean: tidy
	rm -f kernel-loader
	rm -f *.o

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean install
