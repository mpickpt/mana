MPICC = CC
MPIFORTRAN = ftn
default: build
build: fortran_constants.o get_fortran_constants.o
	${MPICC} -I../../include -fPIC -c -g3 -o mpi_logger.o mpi_logger.cpp -lmpi -O0
	${MPICC} -shared -fPIC -o liblogger.so mpi_logger.o get_fortran_constants.o fortran_constants.o
clean:
	rm -f -v *.o *.so

fortran_constants.o: fortran_constants.f90
	${MPIFORTRAN} -g3 -O0 -fPIC -c -o $@ $<

get_fortran_constants.o: 
	cc -g -O2 -std=gnu11 -fPIC -I../../dmtcp/include -I.. -I../lower-half -c -o get_fortran_constants.o get_fortran_constants.c
