## MANA (MPI-Agnostic, Network-Agnostic Checkpointing)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of [DMTCP](https://github.com/dmtcp/dmtcp).  An MPI
program runs under MANA without changes and without relinking.

### Documentation

- [MANA's website](https://mpickpt.github.io/mana/): a quick start, a user
  guide, a developer guide, and the release notes.
- [The MANA man page](manpages/mana.1.md): how to checkpoint and restart, and
  the options of each command.  In a clone, read it with
  `man -l manpages/mana.1`.

MANA's commands (`mana_coordinator`, `mana_launch`, `mana_status` and
`mana_restart`) are wrappers of DMTCP's commands, and they accept DMTCP's
options too.  See [DMTCP's documentation](https://dmtcp.github.io/).

### Building and installing

MANA needs:

- an MPI library: MPICH or a library based on it (such as Cray MPICH), or
  Open MPI 4.1 or 5.0;
- a C++20 compiler, and Python 3 for `mana_launch` and `mana_restart`;
- on Debian and Ubuntu, the package libc6-dbg: MANA needs the symbols of
  the dynamic loader, which these distributions strip.

To build MANA:

    git clone https://github.com/mpickpt/mana
    cd mana
    ./configure      # also checks out DMTCP, MANA's dmtcp submodule
    make -j8

`make` builds only MANA, and puts it in the source tree's `bin` and
`lib/dmtcp`.

`./configure` uses the MPI compilers that come first in `PATH`: `mpicc`,
`mpic++` (or `mpicxx`) and `mpifort` (or `mpif90`), or `cc`, `CC` and `ftn`
with the Cray Programming Environment.  To choose others, set `MPICC`,
`MPICXX` and `MPIFC`.  For example:

    ./configure MPICC=$MPI_HOME/bin/mpicc MPICXX=$MPI_HOME/bin/mpicxx \
                MPIFC=$MPI_HOME/bin/mpifort

Build the program with the same MPI library.  To switch MPI libraries, run
`make clean` first.

A later `git pull` can change the DMTCP commit that MANA uses without
changing `dmtcp/`; `make` then warns, and `git submodule update` checks out
the new commit.

To install MANA, with DMTCP, into another directory, give that directory to
`./configure`:

    ./configure --prefix=$HOME/mana-install
    make -j8
    make install        # 'make uninstall' removes it

MANA's scripts find its other files from their own directory, so keep the
default `--bindir` and `--libdir` (`PREFIX/bin` and `PREFIX/lib`).

In a container that runs as root, Open MPI's `mpirun` needs
`OMPI_ALLOW_RUN_AS_ROOT=1` and `OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1`.

In a container, MANA needs `personality(ADDR_NO_RANDOMIZE)`, which the
default seccomp profile of Docker and Podman forbids: run the container with
`--security-opt seccomp=unconfined` (or a profile that allows it), as MANA's
CI does.

### Quick start

After building MANA, build its example program, `examples/ring`.  Then start
a coordinator, and run the program under MANA on one host:

    make -C examples                               # builds examples/ring
    bin/mana_coordinator
    mpirun -np 4 bin/mana_launch examples/ring     # 60 steps, 1 s each

While it runs, checkpoint it from another shell, in the same directory:

    bin/mana_status --bcheckpoint                  # each rank N writes ckpt_rank_N/

The command returns when the checkpoint is complete.  Then stop the job
(Ctrl-C), and restart it from the checkpoint, with a new coordinator:

    bin/mana_coordinator
    mpirun -np 4 bin/mana_restart                  # goes on from the checkpoint

The restarted job goes on from the step of the checkpoint, not from step 1.
On a cluster, run the same commands in an allocation, with `srun` in place of
`mpirun`.  The man page has the other ways to checkpoint, such as
`mana_status --kcheckpoint` and periodic checkpoints.

### Make targets

| Target | What it does |
|---|---|
| `make` | Builds DMTCP and MANA, and puts them in `bin/` and `lib/dmtcp/`.  It first prints the build environment and, in a git clone, links MANA's git hook. |
| `make mana` | The same build, without printing the environment or linking the hook. |
| `make tests` | Builds the end-to-end tests (`test/`), the example (`examples/ring`) and the benchmarks (`bench/`).  `make` does not build them. |
| `make check` | Builds MANA and the end-to-end tests, and runs the tests with `test/autotest.py`. |
| `make install` | Installs MANA and DMTCP into the `--prefix` of `./configure`.  `make uninstall` removes them. |
| `make tidy` | Removes checkpoint files from the top directory: `ckpt_rank_*`, `ckpt_*.dmtcp` and `dmtcp_restart_script*`. |
| `make clean` | Does `make tidy`, and removes MANA's build outputs in `src/`.  It keeps DMTCP's build and the tests. |
| `make distclean` | Does `make clean` and DMTCP's `make distclean`, and removes `bin/`, `lib/` and the files that `./configure` made.  Run `./configure` again after it. |

To build only one set of programs, run `make` in its directory:
`make -C test` (the end-to-end tests), `make -C examples` (`ring`), or
`make -C bench` (the benchmarks).

### Tests and benchmarks

To run MANA's tests, which checkpoint and restart each test program:

    make tests               # builds the tests, examples and benchmarks
    test/autotest.py         # or: make check
    test/autotest.py --help  # e.g. --launcher 'srun -n {n}'

The benchmarks in `bench/` measure MANA's overhead.  They are built with
`-O2`, and `make check` does not run them.

- `virtid_bench` measures the cost that MANA adds to each wrapped MPI call.
  Run it natively and under MANA, and subtract.
- `synthetic_app` has profiles inspired by the communication of applications
  such as VASP and GROMACS.  A run under MANA must print the same checksum
  as a native run.  See
  [bench/README-synthetic_app.md](bench/README-synthetic_app.md).

`synthetic_app` has less computation and more communication than the
applications that inspired its profiles, so it is closer to a
microbenchmark: MANA's overhead on a real program can be different.

### License

MANA is free software: you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License, version 3 or (at your option)
any later version. This is the same license as DMTCP.  See
[COPYING.LESSER](COPYING.LESSER) for the LGPL and [COPYING](COPYING) for the
GNU GPL, whose terms the LGPL incorporates.  The DMTCP submodule and the
autotools scripts carry their own license notices.

---

To cite this project in a publication, please cite:

* "MANA for MPI: MPI-Agnostic Network-Agnostic Transparent Checkpointing",
  Rohan Garg, Gregory Price, and Gene Cooperman, HPDC'19.
  (original academic prototype)

For other MANA publications with updates to this project, see:
* "Enabling Practical Transparent Checkpointing for MPI: A Topological
     Sort Approach",
   Yao Xu and Gene Cooperman,
   IEEE International Conference on Cluster Computing (Cluster'24)

* "Implementation-Oblivious Transparent Checkpoint-Restart for MPI",
  Yao Xu, Leonid Belyaev, Twinkle Jain,
    Derek Schafer, Anthony Skjellum, Gene Cooperman, SuperCheck-SC23 Workshop
	at SC'23.
  (near production version of MANA)
