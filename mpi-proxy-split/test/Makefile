PLATFORM=${shell echo $$HOST}
include ../Makefile_config

###############################################################
# NOTE:  More Fortran/C examples (under LGPL license) at:
#        https://people.math.sc.edu/Burkardt/f_src/mpi/mpi.html
###############################################################

ifndef DMTCP_ROOT
  DMTCP_ROOT=../../dmtcp
endif
DMTCP_BIN=${DMTCP_ROOT}/bin
DMTCP_LIB=${DMTCP_ROOT}/lib/dmtcp
DMTCP_INCLUDE=${DMTCP_ROOT}/include
# LIBNAME=libdmtcp_mpi-proxy
LIBNAME=libmana

# Examples that the documentation uses (mana_launch ping_pong.mana.exe).
FILES = mpi_hello_world ping_pong

OBJS=$(addsuffix .o, ${FILES})

TESTS_DUMMY=$(addsuffix .mana.exe, ${FILES})
TESTS=$(addsuffix .exe, ${FILES})
LDFLAGS_DUMMY=-L$$PWD/../mpi-wrappers -lmpistub

# Benchmarks of MANA's overhead; built with -O2 and not run by 'make check'.
BENCH_FILES=virtid_bench synthetic_app
BENCHES=$(addsuffix .exe, ${BENCH_FILES}) \
        $(addsuffix .mana.exe, ${BENCH_FILES})

default: default_prereqs
	+ $(MAKE) default_part2

default_part2: ${TESTS} ${TESTS_DUMMY} ${BENCHES} e2e

# End-to-end tests, run by autotest.py natively and under MANA.  Each test is
# one program, named after the test and linked as any MPI program.
E2E_TESTS = p2p_blocking p2p_nonblocking p2p_ring p2p_any_source p2p_probe \
            p2p_large p2p_proc_null collectives nonblocking_collectives \
            communicators cartesian datatypes memory init_thread \
            exit_after_finalize attributes misc_calls \
            finalize_unsync abort
E2E_FORTRAN = fortran_p2p fortran_nonblocking

e2e: ${E2E_TESTS} ${E2E_FORTRAN}

${E2E_TESTS}: %: %.c mana_test.h
	$(MPICC) -g3 -O0 -o $@ $< $(MPI_CFLAGS) ${MPI_LDFLAGS}

${E2E_FORTRAN}: %: %.f90
	$(MPIFORTRAN) ${FFLAGS} -g3 -O0 -o $@ $<

check: e2e
	./autotest.py

default_prereqs:
	@ # The darshan module is used at NERSC.
	@ if command -v module >/dev/null && module list 2>&1| grep -q darshan;\
	  then \
	    echo '** DMTCP is incompatible with the darshan module.'; \
	    echo '** Please do "module unload darshan" before building DMTCP.';\
	    exit 1; \
	fi
	@ # The altd module is used at NERSC.
	@ if command -v module >/dev/null && module list 2>&1| grep -q altd;\
	  then \
	    echo '** DMTCP is incompatible with the altd module.'; \
	    echo '** Please do "module unload altd" before building DMTCP.';\
	    exit 1; \
	fi

%.o: %.c
	 $(MPICC) -g3 -O0 -c -o $@ $< $(MPI_CFLAGS)

$(addsuffix .o, ${BENCH_FILES}): %.o: %.c
	 $(MPICC) -g -O2 -c -o $@ $< $(MPI_CFLAGS)

%.exe: %.f90
	$(MPIFORTRAN) ${FFLAGS} -g3 -O0 -o $@ $<

%.mana.exe: %.f90
	$(MPIFORTRAN) ${FFLAGS} -g3 -O0 -o $@ $< ${LDFLAGS_DUMMY}

%.exe: %.o
	 $(MPICC) -o $@ $< ${MPI_LDFLAGS}

%.mana.exe: %.o
	 $(CC) -o $@ $< ${MPI_LDFLAGS} ${LDFLAGS_DUMMY}

%.exe: %.cpp
	$(MPICXX) -g3 -O0 -o $@ $< $(MPI_CXXFLAGS)

tidy:
	rm -f ckpt_*.dmtcp dmtcp_restart_script* \
	dmtcp-shared-memory.* dmtcp-test-typescript.tmp core*
	rm -rf ckpt_*
	rm -f dmtcp_coordinator_db-*.json

clean: tidy
	rm -f $(TESTS) ${OBJS} ${TESTS_DUMMY} ${BENCHES} *.pyc *.so
	rm -f ${E2E_TESTS} ${E2E_FORTRAN}
