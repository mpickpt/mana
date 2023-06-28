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
DEMO_PORT=7787

# FILES=mpi_hello_world Abort_test Allreduce_test Alltoall_test \
#       Alltoallv_test Barrier_test Bcast_test Comm_split_test Reduce_test \
#       send_recv send_recv_many ping_pong Comm_dup_test Sendrecv_test \
#       Waitall_test Allgather_test Group_size_rank Type_commit_contiguous \
#       Irecv_test Alloc_mem f_ibarrier file_test Waitany_test \
#       Comm_get_attr_test
## Reordering the tests:
# Several tests are excluded from this Makefile because they are redundant:
# multi_send_recv, send_recv, send_recv_many
# Comment out FILES_FORTRAN if you don't have an mmpifort compiler.
FILES_FORTRAN= f_ibarrier wave_mpi day1_mpi quad_mpi poisson_nonblock_mpi
FILES=mpi_hello_world \
      hello_mpi_init_thread Sendrecv_test Gatherv_test \
      keyval_test file_test Scatterv_test Gather_test \
      ping_pong Comm_dup_test Scan_test Scatter_test \
      Comm_get_attr_test Initialized_test Cart_sub_test\
      Barrier_test Bcast_test Comm_split_test Reduce_test \
      Waitall_test Waitany_test Testany_test \
      two-phase-commit-1 two-phase-commit-2 two-phase-commit-3 \
      Ibcast_test Ibarrier_test Isend_test \
      Cart_map_test Cartdim_get_test \
      Abort_test Allreduce_test Alltoall_test Alltoallv_test \
      Allgather_test Group_size_rank Type_commit_contiguous \
      Irecv_test Alloc_mem sendrecv_replace_test \
      Type_hvector_test Type_vector_test \
      ${FILES_FORTRAN} \
      send_recv_loop large_async_p2p \
	  File_read_write_test File_characteristics_test File_size_test \
	  File_read_write_all_test \
      Type_dup_test Type_create_hindexed_test \
	  Type_create_resized_test Type_create_hindexed_block_test

OBJS=$(addsuffix .o, ${FILES})

TESTS_DUMMY=$(addsuffix .mana.exe, ${FILES})
TESTS=$(addsuffix .exe, ${FILES})
LDFLAGS_DUMMY=-L$$PWD/../mpi-wrappers -lmpistub

default: default_prereqs
	$(MAKE) default_part2

default_part2: ${TESTS} ${TESTS_DUMMY}

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

# This doesn't work well yet, but it's a work in progress.
# PROBLEM WITH UNAVAILABLE NODES:
#  srun: step creation temporarily disabled, retrying (Requested nodes are busy
check-%: ${TESTS_DUMMY}
	${MAKE} tidy > /dev/null; \
	${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	pkill -9 mana.exe; \
	pkill -9 srun; \
	echo ""; \
	echo ""; \
	echo '***' Testing $*.mana.exe '***'; \
	${DMTCP_BIN}/dmtcp_coordinator -q -q --exit-on-last --daemon & \
	pid_last=$$! ; \
	(sleep 10 && pkill -9 mana.exe && pkill -9 srun) & \
	srun -n 2 ${DMTCP_BIN}/dmtcp_launch --host `hostname` --no-gzip --join \
                --disable-dl-plugin --with-plugin \
                ${DMTCP_LIB}/libmana.so -i5 ./$*.mana.exe; \
	sleep 10; \
	${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	pkill -9 mana.exe; \
	pkill -9 srun; \
	echo ""; \
	echo '***' Restarting $*.mana.exe '***'; \
	echo $$pid_last; \
	wait $$pid_last; \
	${DMTCP_BIN}/dmtcp_coordinator -q -q --exit-on-last --daemon; \
	(sleep 10 && pkill -9 srun) & \
	srun -n 2 ${DMTCP_BIN}/dmtcp_restart --host `hostname` --join --mpi \
	     ckpt_rank_*/ckpt_*.dmtcp; \
	sleep 10 \
	${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	pkill -9 mana.exe; \
	echo '***' Done testing $*.mana.exe '***'; \
	sleep 10;
check: ${TESTS_DUMMY}
	for mpi_test in $^; do \
	  ${MAKE} tidy > /dev/null; \
	  ${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	  pkill -9 mana.exe; \
	  pkill -9 srun; \
	  echo ""; \
	  echo ""; \
	  echo '***' Testing "$$mpi_test" '***'; \
	  ${DMTCP_BIN}/dmtcp_coordinator -q -q --exit-on-last --daemon; \
	  (sleep 10 && pkill -9 mana.exe && pkill -9 srun) & \
	  srun -n 2 ${DMTCP_BIN}/dmtcp_launch --no-gzip --join \
                  --disable-dl-plugin --with-plugin \
                  ${DMTCP_LIB}/libmana.so -i5 ./$$mpi_test; \
	  sleep 10; \
	  ${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	  pkill -9 mana.exe; \
	  pkill -9 srun; \
	  echo ""; \
	  echo '***' Restarting "$$mpi_test" '***'; \
	  ${DMTCP_BIN}/dmtcp_coordinator -q -q --exit-on-last --daemon; \
	  (sleep 10 && pkill -9 srun) & \
	  srun -n 2 ${DMTCP_BIN}/dmtcp_restart --join --mpi \
	       ckpt_rank_*/ckpt_*.dmtcp; \
	  sleep 10 \
	  ${DMTCP_BIN}/dmtcp_command -q 2>/dev/null; \
	  pkill -9 mana.exe; \
	  echo '***' Done testing "$$mpi_test" '***'; \
	  sleep 10; \
	done

integrated_dmtcp_test: integrated_dmtcp_test.c
	${MPICC} -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -DDMTCP -g3 -O0 -o $@ $<

check-integrated_dmtcp_text: tidy integrated_dmtcp_test
	${DMTCP_ROOT}/bin/dmtcp_launch --port ${DEMO_PORT}\
    --with-plugin $$PWD/../${LIBNAME}.so ./integrated_dmtcp_test && \
      echo "Restarting..." && \
        ${DMTCP_ROOT}/bin/dmtcp_restart --port ${DEMO_PORT} \
          ckpt*.dmtcp

%.o: %.c
	 $(MPICC) -g3 -O0 -c -o $@ $< $(MPI_CFLAGS)

# Allowing fortran argument mismatch in order to allow wave and day1 tests to
# compile with GNU compiler. Note that this should not affect execution
# Intel/GNU compilers on Cray Haswell
# Argument mistmatch is a legacy feature that newer fortran compilers do not
# allow, but is still used in two of our test cases
%.exe: %.f90
	$(MPIFORTRAN) ${FFLAGS} -g3 -O0 -o $@ $<

%.mana.exe: %.f90
	$(MPIFORTRAN) ${FFLAGS} -g3 -O0 -o $@ $<
	  ${LDFLAGS_DUMMY}

%.exe: %.o
	 $(MPICC) -o $@ $< ${MPI_LDFLAGS}

%.mana.exe: %.o
	 $(MPICC) -o $@ $< ${MPI_LDFLAGS} ${LDFLAGS_DUMMY}

%.exe: %.cpp
	$(MPICXX) -g3 -O0 -o $@ $< $(MPI_CXXFLAGS)

tidy:
	rm -f ckpt_*.dmtcp dmtcp_restart_script* \
	dmtcp-shared-memory.* dmtcp-test-typescript.tmp core*
	rm -rf ckpt_*

clean: tidy
	rm -f $(TESTS) ${OBJS} ${TESTS_DUMMY} *.pyc *.so
