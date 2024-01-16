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
PLUGIN_INCLUDE=${PLUGIN_ROOT}

override CFLAGS += -fPIC -I${DMTCP_INCLUDE} -g3 -O0
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${DMTCP_ROOT}/src

TEXTSEG_ADDR_FLAG=-Wl,-Ttext-segment=E000000

default: kernel-loader

.c.o:
	${MPICC} ${CFLAGS} -c -o $@ $<

kernel-loader: kernel-loader.c get-symbol-offset.o copy-stack.o patch-trampoline.o
	${MPICC} -g3 ${TEXTSEG_ADDR_FLAG} -o $@ $^

check-mmap: libmmap.so
	LD_PRELOAD=$$PWD/libmmap.so ls
libmmap.so: mmap64.c munmap.c
	${CC} -shared ${CFLAGS} -DLIBMMAP_SO -I. -g3 -O0 -fPIC -o libmmap.so $^

install: kernel-loader
	cp -f $^ ${MANA_ROOT}/bin/

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*
	rm -rf dmtcp_coordinator_db-*.json

clean: tidy
	rm -r kernel-loader
	rm -f ${PROXY_BIN} ${PROXY_BIN_DEFADDR} ${LIBPROXY}.a ${PROXY_OBJS} ${LIBPROXY_OBJS}
	rm -f libmmap.so
	rm -f ${MANA_ROOT}/bin/${PROXY_BIN}
	rm -f ${MANA_ROOT}/bin/${PROXY_BIN_DEFADDR}
	if test -d gethostbyname-static; then \
	  cd gethostbyname-static && make clean; \
	fi

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean install
