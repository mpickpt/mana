# synthetic_app

`synthetic_app` is a synthetic MPI program to measure MANA's overhead and to
test checkpoint and restart.  Each of its profiles is a synthetic test
inspired by the communication of an application or of a kind of
application.  It contains no code from these applications.

- `halo`, inspired by stencil codes: a halo exchange with nonblocking
  point-to-point calls
- `vasp`, inspired by the collectives of VASP: many small blocking
  collectives on sub-communicators
- `gromacs`, inspired by the PP/PME decomposition of GROMACS: a Sendrecv
  halo plus PP/PME exchanges
- `eddiag`, inspired by the subspace diagonalization of VASP: large
  Allreduces in chunks, then memory-bound passes
- `sumbcast`, inspired by the sums over communicators in VASP: Send/Recv to
  a root, then Bcast, on 16- or 32-rank communicators

Every rank checks the values that it receives and adds them to a checksum.
A run under MANA must print the same checksum as a native run with the
same options.

## Usage

```
make -C mpi-proxy-split/test synthetic_app
mpirun -np 8 mpi-proxy-split/test/synthetic_app --profile=vasp
bin/mana_coordinator
mpirun -np 8 bin/mana_launch mpi-proxy-split/test/synthetic_app --profile=vasp
```

The options are listed at the top of `synthetic_app.c`.
