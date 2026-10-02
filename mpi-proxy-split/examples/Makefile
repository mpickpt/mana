# Example MPI programs (see ring.c).  'make' builds them with the MPI
# compiler that ./configure found, as for any MPI program.
include ../Makefile_config

EXAMPLES = ring

default: ${EXAMPLES}

${EXAMPLES}: %: %.c
	${MPICC} -g -O2 -o $@ $<

clean:
	rm -f ${EXAMPLES}

.PHONY: default clean
