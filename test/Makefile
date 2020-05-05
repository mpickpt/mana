
LN_S=ln -s
OPENMP_CFLAGS=-fopenmp
OPENMP_CXXFLAGS=-fopenmp
CC = gcc
M32=0
MULTILIB=@MULTILIB@
# Unfortunately, there is a name clash for 'DEBUG'.
# --enable-debug intends to set 'DEBUG' in src, but sets it everywhere.
# Here in 'test', we use 'DEBUG' for one-off tests.  So, we unset 'DEBUG'.
CFLAGS = -g -O2 -Wall -g3 -O0 -DDEBUG -UDEBUG
CPPFLAGS =  -Wall -g3 -O0 -DDEBUG
CXX = g++
CXXFLAGS = -g -O2 -Wall -g3 -O0 -DDEBUG
HAS_JAVA= yes
HAS_JAVAC= yes
HAS_CMA= yes
LDFLAGS = 
MKDIR_P = /usr/bin/mkdir -p
PACKAGE = dmtcp
PACKAGE_BUGREPORT = dmtcp-forum@lists.sourceforge.net
PACKAGE_NAME = DMTCP
PACKAGE_STRING = DMTCP 3.0.0
PACKAGE_TARNAME = dmtcp
PACKAGE_URL = http://dmtcp.sourceforge.net
PACKAGE_VERSION = 3.0.0
prefix=/usr/local
exec_prefix=${prefix}
datarootdir=${prefix}/share
bindir = ${exec_prefix}/bin
docdir = ${datarootdir}/doc/${PACKAGE_TARNAME}
includedir = ${prefix}/include
infodir = ${datarootdir}/info
libdir = ${exec_prefix}/lib
pkglibdir = $(libdir)/dmtcp
top_builddir = ..
top_srcdir = ..
srcdir = .

targetdir = $(top_builddir)

ifeq (${shell uname -m},aarch64)
else
ifeq (${shell uname -m},armv7)
else
ifeq (${shell uname -m},armv7l)
else
DO_PTHREAD_ATFORK = yes
endif
endif
endif

DMTCP_INCLUDE=${top_srcdir}/include

CFLAGS += -I${DMTCP_INCLUDE} -DDMTCP -fPIC -g -std=gnu99
CXXFLAGS += -I${DMTCP_INCLUDE} -DDMTCP -fPIC -g


ifeq ($(M32),1)
  targetdir = $(top_builddir)/lib/$(PACKAGE)/32
  CFLAGS += -m32 -Wa,--32
  CXXFLAGS += -m32 -Wa,--32
  LDFLAGS += -m32 -Wl,-m32 -Wl,-melf_i386 -Wa,--32
else
  HAS_READLINE=no
  READLINE_LIBS=-lreadline -lhistory -lcurses
  HAS_MPICH=no
  MPICH_MPICC=
  HAS_OPENMPI=no
  OPENMPI_MPICC=no
  HAS_CILK=no
  CILKC=no
endif

# If MULTILIB, then test if we can ckpt/restart a 32-bit app
ifeq ($(MULTILIB),1)
  TESTS_MULTILIB = dmtcp1-m32
endif

PWD=$(shell pwd)

TESTS=${notdir ${basename ${shell ls $(srcdir)/*.c $(srcdir)/*.cpp $(srcdir)/*.cilk | grep -v 'hellompi.c'}}} \
	${addsuffix .class,${notdir ${basename ${shell ls $(srcdir)/java*.java}}}} \
	hellompich openmpi

tests: $(TESTS) $(TESTS_MULTILIB)
	cd plugin && ${MAKE}
	#${MAKE} -C credentials

tidy:
	rm -f ckpt_*.dmtcp dmtcp_restart_script* \
	  dmtcp-shared-memory.* dmtcp-test-typescript.tmp core*
	rm -rf ckpt_*
	cd plugin && $(MAKE) tidy > /dev/null

clean: tidy
	rm -f $(TESTS) $(TESTS_MULTILIB) *.pyc *.so
	#${MAKE} -C credentials clean
	cd plugin && $(MAKE) clean

distclean: clean
	cd plugin && $(MAKE) distclean
	#${MAKE} -C credentials distclean
	rm -f Makefile

# plugin-init will call the INIT event in libdmtcp_plugin-init.so
libdmtcp_plugin-init.so: plugin-init.cpp
	${CXX} ${CXXFLAGS} -shared -fPIC -o $@ $<
plugin-init: libdmtcp_plugin-init.so
	# Don't create executable.  Only the library, above, used on test/sleep1

readline: readline.c
ifeq ($(HAS_READLINE),yes)
	$(CC) -o $@ $< $(CFLAGS) $(READLINE_LIBS)
else
	#$@: required libraries missing, skipping test
endif

syscall-tester: syscall-tester.c
	$(CC) -o $@ $< $(CFLAGS) -rdynamic

timer%: timer%.c
	$(CC) -o $@ $< $(CFLAGS) -lrt

clock: clock.c
	$(CC) -o $@ $< $(CFLAGS) -lrt

# Assume OpenMP exists only if OPENMP_CFLAGS is set.
openmp-%: openmp-%.c
ifneq (${OPENMP_CFLAGS},)
	-${CC} ${CFLAGS} ${OPENMP_CFLAGS} -o $@ $<
else
	#$@: This ${CC} does not support OpenMP, skipping test
endif

# Assume OpenMP exists for C++ only if OPENMP_CXXFLAGS is set.
openmp-%: openmp-%.cpp
ifneq (${OPENMP_CXXFLAGS},)
	-${CXX} ${CXXFLAGS} ${OPENMP_CXXFLAGS} -o $@ $<
else
	#$@: This ${CXX} does not support OpenMP, skipping test
endif

hellompich: hellompi.c
ifeq ($(HAS_MPICH),yes)
	- $(MPICH_MPICC) -o $@ $< $(CFLAGS)
else
	#$@: required libraries missing, skipping test
endif

openmpi: hellompi.c
ifeq ($(HAS_OPENMPI),yes)
	- $(OPENMPI_MPICC) -o $@ $< $(CFLAGS)
else
	#$@: required libraries missing, skipping test
endif

DMTCPPLUGIN_HEADER=$(DMTCP_INCLUDE)/dmtcp.h

%-m32: %.c
	-$(CC) -m32 -o $@ $< $(CFLAGS)

dmtcp3: dmtcp3.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

dmtcp4: dmtcp4.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

dmtcp5: dmtcp5.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

pthread%: pthread%.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

mutex%: mutex%.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

# FIXME:  We should create a test in configure.ac to see if this compiles.
ifeq (${DO_PTHREAD_ATFORK},yes)
libpthread_atfork1.so: pthread_atfork1.c
	${CC} ${CFLAGS} -shared -fPIC -DLIB -o $@ $<
pthread_atfork1: pthread_atfork1.c libpthread_atfork1.so
	${CC} ${CFLAGS} -o $@ $< -L${PWD} -lpthread_atfork1 -lpthread

libpthread_atfork2.so: pthread_atfork2.c
	${CC} ${CFLAGS} -shared -fPIC -DLIB -o $@ $<
pthread_atfork2: pthread_atfork2.c libpthread_atfork2.so
	${CC} ${CFLAGS} -o $@ $< -L${PWD} -lpthread_atfork2 -lpthread
else
# FIXME:  On aarch64, gcc-4.9.2, libcg-2.19, pthread_atfork error:
#    /usr/bin/ld: pthread_atfork: hidden symbol `pthread_atfork' in ...
#                                 is referenced by DSO
pthread_atfork1: pthread_atfork1.c
	@ echo "FIXME: pthread_atfork is not being compiled for arm/aarch64"
pthread_atfork2: pthread_atfork2.c
	@ echo "FIXME: pthread_atfork is not being compiled for arm/aarch64"
endif

ifeq (${HAS_CMA},yes)
cma: cma.c
	$(CC) -o $@ $< $(CFLAGS)
else
cma: cma.c
	@ echo "#cma: Skipping test for cma.  CMA requires Linux 3.2 or later."
endif

posix-mq%: posix-mq%.c
	-$(CC) -o $@ $< $(CFLAGS) -lrt

inotify%: inotify%.c
	-$(CC) -o $@ $< $(CFLAGS) -lpthread

# dlopen1 will dlopen/dlclose libdlopen-lib[12].so
libdlopen-lib1.so: dlopen1.c
	${CC} ${CFLAGS} -shared -fPIC  -DLIB1 -o $@ $<
libdlopen-lib2.so: dlopen1.c
	${CC} ${CFLAGS} -shared -fPIC  -DLIB2 -o $@ $<
dlopen1: dlopen1.c libdlopen-lib1.so libdlopen-lib2.so
	${CC} $(CFLAGS) -o $@ $< -ldl

# dlopen2 will dlopen/dlclose libdlopen-lib[34].so
libdlopen-lib3.so: dlopen2.cpp
	${CXX} ${CXXFLAGS} -shared -fPIC -DLIB3 -o $@ $<
libdlopen-lib4.so: dlopen2.cpp
	${CXX} ${CXXFLAGS} -shared -fPIC -DLIB4 -o $@ $<
dlopen2: dlopen2.cpp libdlopen-lib3.so libdlopen-lib4.so
	${CXX} ${CXXFLAGS} -o $@ $< -ldl

%.class: %.java
ifeq ($(HAS_JAVAC),yes)
	javac $< -d .
else
	#$@: required 'javac' missing, skipping test
endif

cilk%: cilk%.cilk
ifeq ($(HAS_CILK),yes)
	${CILKC} -o $@ $<
else
	#$@: required 'cilkc' missing, skipping test
endif

%: %.c
	$(CC) -o $@ $< $(CFLAGS)

%: %.cpp
	$(CXX) -o $@ $< $(CXXFLAGS)
