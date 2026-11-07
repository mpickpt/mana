---
title: Install
nav_order: 3
description: "Requirements, build options, and installation of MANA."
---

# Install
{: .no_toc }

1. TOC
{:toc}

---

## Requirements

MANA needs these items:

- **An MPI library.** MPICH, a library based on MPICH (for example, Cray
  MPICH), or Open MPI 4.1 or 5.0. The application must link the MPI library
  as a shared library. This is the default of the MPI compilers.
- **A C++20 compiler**, a Fortran compiler, and GNU make.
- **Python 3**, for `mana_launch` and `mana_restart`.
- **On Debian and Ubuntu, the package `libc6-dbg`.** MANA needs the symbols of
  the dynamic loader, `ld.so`. These distributions remove the symbols from
  `ld.so`, and `libc6-dbg` supplies them.
- **Git**, if you build from a clone. MANA gets DMTCP as a git submodule.

For example, on Ubuntu 26.04 with MPICH:

```bash
sudo apt-get install build-essential gfortran git python3 libc6-dbg \
                     mpich libmpich-dev
```

For Open MPI, replace `mpich libmpich-dev` with `openmpi-bin libopenmpi-dev`.

## Build MANA

```bash
git clone https://github.com/mpickpt/mana
cd mana
./configure
make -j8
```

`./configure` checks out DMTCP into `dmtcp/` if that directory is empty.
`make` builds DMTCP and MANA. It puts the MANA commands and DMTCP commands in
`bin/`, and the libraries in `lib/dmtcp/`. `make` does not build the tests,
the example or the benchmarks. `make tests` builds them.

## Choose the MPI library

`./configure` uses the MPI compilers that come first in `PATH`:

- `mpicc`, `mpic++` (or `mpicxx`), and `mpifort` (or `mpif90`).
- With the Cray Programming Environment, `cc`, `CC` and `ftn`.

To use other compilers, set `MPICC`, `MPICXX` and `MPIFC`:

```bash
./configure MPICC=$MPI_HOME/bin/mpicc MPICXX=$MPI_HOME/bin/mpicxx \
            MPIFC=$MPI_HOME/bin/mpifort
```

Build the MPI program with the same MPI library as MANA, and run it with the
launcher of that library, for example `$MPI_HOME/bin/mpirun`.

To change to a different MPI library:

1. Copy the checkpoints that you want to keep to a different directory.
   `make clean` deletes the checkpoints in the top directory.
2. Run `make clean`. Also run `make -C test clean`, `make -C examples clean`
   and `make -C bench clean`. `make clean` keeps these programs, and they use
   the old library.
3. Run `./configure` with the new compilers, then `make`.

## Build options

| Option | What it does |
|---|---|
| `--prefix=DIR` | Sets the installation directory for `make install`. |
| `--enable-debug` | Compiles with `-g3 -O0` and debug messages. A debug build is much slower. Do not use it to measure performance. |

## Install MANA into a directory

By default, you run MANA from its source tree. To install MANA and DMTCP into
a different directory, give that directory to `./configure`:

```bash
./configure --prefix=$HOME/mana-install
make -j8
make install
```

The commands are then in `$HOME/mana-install/bin`. `make uninstall` deletes
them. `make install` also accepts `DESTDIR`.

{: .important }
The MANA scripts find the other MANA files from the directory of the script.
So keep the default `--bindir` and `--libdir` (`PREFIX/bin` and `PREFIX/lib`).

## Make targets

| Target | What it does |
|---|---|
| `make` | Builds DMTCP and MANA into `bin/` and `lib/dmtcp/`. It first prints the build environment. In a git clone, it also links the MANA git hook. |
| `make mana` | Does the same build, but does not print the environment or link the hook. |
| `make tests` | Builds the end-to-end tests (`test/`), the example (`examples/ring`), and the benchmarks (`bench/`). |
| `make check` | Builds MANA and the tests, and runs the tests with `test/autotest.py`. |
| `make install` | Installs MANA and DMTCP into the `--prefix` directory. `make uninstall` deletes them. |
| `make tidy` | Deletes the checkpoint files in the top directory: `ckpt_rank*`, `ckpt_*.dmtcp`, `dmtcp_restart_script*` and `dmtcp_coordinator_db-*.json`. |
| `make clean` | Does `make tidy`, and deletes the MANA build outputs in `src/`. It keeps the DMTCP build and the tests. |
| `make distclean` | Does `make clean` and the `make distclean` of DMTCP. It also deletes `bin/`, `lib/` and the files that `./configure` made. Run `./configure` again after it. |

To build one set of programs, run `make` in its directory: `make -C test`,
`make -C examples`, or `make -C bench`.

## Update a clone

A `git pull` can change the DMTCP commit that MANA uses, but it does not
change the contents of `dmtcp/`. In this case, `make` prints a warning. To
get the new DMTCP commit, type:

```bash
git submodule update
```

{: .warning }
MANA 1.6.0 moved the sources from `mpi-proxy-split/` to `src/`, `test/`,
`examples/` and `bench/`. To update a clone of an older release, run
`make distclean` before `git pull`, and run `./configure` after it.

## Containers

MANA works in Docker and Podman containers, with two settings:

- MANA turns off address space randomization with
  `personality(ADDR_NO_RANDOMIZE)`. The default seccomp profile of Docker and
  Podman does not allow this call. So, start the container with
  `--security-opt seccomp=unconfined`, or with a profile that allows the call.
  The continuous integration of MANA uses this option.
- MPICH needs more than 64 MB of shared memory (`/dev/shm`), the default
  size in Docker. So, start the container with `--shm-size=2g`, as the
  continuous integration of MANA does.
- A container often runs as root. The `mpirun` of Open MPI does not run as root
  unless you set `OMPI_ALLOW_RUN_AS_ROOT=1` and
  `OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1`.

## Source tarball

The "Source code" archives of a GitHub release do not contain the DMTCP
submodule. Each release from 1.6.0 also has a tarball that contains DMTCP.
Use that tarball if you cannot use git.
