# MPI compiler according to the platform
UNAME_RELEASE=${shell uname -r}
include ../Makefile_config

PROXY_BIN = lh_proxy
PROXY_BIN_DEFADDR = lh_proxy_da
PROXY_OBJS = lh_proxy.o
LIBPROXY = libproxy
LIBPROXY_OBJS = libproxy.o procmapsutils.o sbrk.o mmap64.o munmap.o \
		shmat.o shmget.o

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

PROXY_LD_FLAGS=-static -Wl,--wrap -Wl,__munmap -Wl,--wrap -Wl,shmat -Wl,--wrap -Wl,shmget

TEXTSEG_ADDR_FLAG=-Wl,-Ttext-segment=E000000

default: ${PROXY_BIN} ${PROXY_BIN_DEFADDR} ${STATIC_GETHOSTBYNAME}

# We should remove the special case for Cori, once we know this works on Cori.
ifeq (${PERLMUTTER}, 1)
  STATIC_GETHOSTBYNAME=
else
  STATIC_GETHOSTBYNAME=gethostbyname-static/gethostbyname_static.o
${STATIC_GETHOSTBYNAME}: gethostbyname-static/gethostbyname_static.c \
                         gethostbyname-static/gethostbyname_proxy.c
	cd gethostbyname-static && make gethostbyname_static.o
	cd gethostbyname-static && make gethostbyname_proxy
	cp -f gethostbyname-static/gethostbyname_proxy ${MANA_ROOT}/bin/
endif

ifeq ($(findstring cray,$(UNAME_RELEASE)), cray)
  STATIC_LIBS=true
else
  STATIC_LIBS=false
endif

.c.o:
	${MPICC} ${CFLAGS} -c -o $@ $<

# CAVEATS:
# If the Makefile complains about a missing libxml2.a, it can be because of
#   a bad MPICH configuration.  If MPICH discovers libxml2.so, then it
#   configures itself to require libxml2, even though '-static libxml2.a' fails.
#   The solution is to install the distro package libxml2-dev .
# Ubuntu mpich package adds '-lcr' flag that's incompatible with 'mpicc -static'
# MPICH-3.3 'mpicc -static' adds spurious duplicate -lunwind; we remove that.
# *** NOTE:  This will fail unless you have configured MPICH with:
# ***        ./configure --disable-xml2 --disable-libxml2 ...
# ***        or unless you installed the necessary libaries.
# ***        See ./README.mpich-static here, for how to install the libaries
# NOTE:  The static library libnl-3.a must be built specially.  A suggested recipe is:
# Search for and download libnl3-3.3.0-1.29.src.rpm
# rpm -i libnl3-3.3.0-1.29.src.rpm
# rpmbuild -ba ~/rpmbuild/SPECS/libnl3.spec
# cd ~/rpmbuild/BUILD/libnl-3.3.0/lib/.libs/
# Build libnl-3.a from lib_libnl_3_la-*.o
# NOTE: -zmuldefs used in static_libs.txt because libjansson.a and libjson.a both 
# want to define 'json_object_get'. We prefer libjansson.a.
# static_libs.txt has json_object_get multiply defined; using -Xlinker -zmuldefs
${PROXY_BIN}: ${PROXY_OBJS} ${LIBPROXY}.a ${STATIC_GETHOSTBYNAME}
	if ${MPICC} -v 2>&1 | grep -q 'MPICH version'; then \
	  rm -f tmp.sh; \
	  ${MPICC} -show ${PROXY_LD_FLAGS} ${TEXTSEG_ADDR_FLAG} -o $@ -Wl,-start-group \
	    $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -ldl -Wl,-end-group | \
	    sed -e 's^-lunwind ^ ^'> tmp.sh; \
	  sh tmp.sh; \
	  rm -f tmp.sh; \
	elif ${STATIC_LIBS}; then \
	  make libnl3.a; \
	  ${MPICC} ${PROXY_LD_FLAGS} ${TEXTSEG_ADDR_FLAG} -o $@ -Wl,-start-group \
            $^ ${MPI_LD_FLAG} `cat static_libs.txt` -ldl -Wl,--end-group; \
	else \
	  ${MPICC} ${PROXY_LD_FLAGS} ${TEXTSEG_ADDR_FLAG} -o $@ -Wl,-start-group \
            $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -ldl -Wl,-end-group; \
	fi

${PROXY_BIN_DEFADDR}: ${PROXY_OBJS} ${LIBPROXY}.a ${STATIC_GETHOSTBYNAME}
	if ${MPICC} -v 2>&1 | grep -q 'MPICH version'; then \
	  rm -f tmp1.sh; \
	  ${MPICC} -show ${PROXY_LD_FLAGS} -o $@ -Wl,-start-group \
	    $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -ldl -Wl,-end-group | \
	    sed -e 's^-lunwind ^ ^'> tmp1.sh; \
	  sh tmp1.sh; \
	  rm -f tmp1.sh; \
	elif ${STATIC_LIBS}; then \
	  make libnl3.a; \
	  ${MPICC} ${PROXY_LD_FLAGS} -o $@ -Wl,-start-group \
            $^ ${MPI_LD_FLAG} `cat static_libs.txt` -ldl -Wl,--end-group; \
	else \
	  ${MPICC} ${PROXY_LD_FLAGS} -o $@ -Wl,-start-group \
	  $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -ldl -Wl,-end-group; \
	fi

# NOTE:  On Perlmutter at NERSC, the static library libnl3.a (required by
#        libmpich.a since Slingshot Host 2.1) must be built specially.
libnl3.a: libnl3-3.3.0-1.29.src.rpm
	rpm --define "_topdir $${PWD}/rpmbuild" -i $<
	rpmbuild --define "_topdir $${PWD}/rpmbuild" -ba \
	  $${PWD}/rpmbuild/SPECS/libnl3.spec
	ar r libnl3.a \
	  $${PWD}/rpmbuild/BUILD/libnl-3.3.0/lib/.libs/lib_libnl_3_la-*.o

${LIBPROXY}.a: ${LIBPROXY_OBJS}
	ar cr $@ $^

check-mmap: libmmap.so
	LD_PRELOAD=$$PWD/libmmap.so ls
libmmap.so: mmap64.c munmap.c
	${CC} -shared ${CFLAGS} -DLIBMMAP_SO -I. -g3 -O0 -fPIC -o libmmap.so $^

install: ${PROXY_BIN} ${PROXY_BIN_DEFADDR} ${STATIC_GETHOSTBYNAME}
	cp -f $^ ${MANA_ROOT}/bin/

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*
	rm -rf dmtcp_coordinator_db-*.json

clean: tidy
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
