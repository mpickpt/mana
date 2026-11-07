---
title: Testing
parent: Developer guide
nav_order: 5
description: "The end-to-end tests, the benchmarks, and the continuous integration of MANA."
---

# Testing
{: .no_toc }

1. TOC
{:toc}

---

## Run the tests

```bash
make                    # builds MANA
make tests              # builds the tests, the example and the benchmarks
test/autotest.py        # or: make check
```

`test/autotest.py` runs each test program under MANA. It checkpoints and
restarts most of them. A few tests, such as `init_thread` and `abort`, run
without a checkpoint. It prints one line per test, and a summary at the end. A full run
takes some minutes.

Useful options:

| Option | What it does |
|---|---|
| `TEST ...` | Runs only the named tests. |
| `--list` | Lists the tests, with the number of ranks of each. |
| `--cycles N` | Does N checkpoint-restart cycles per test that runs in a loop. The default is 2. |
| `--launcher 'srun -n {n}'` | Sets the MPI launcher. `{n}` is the number of ranks. The default is `mpirun -np {n}`. Some tests have more ranks than a small computer has cores. Open MPI then needs `mpirun --oversubscribe -np {n}`. |
| `--native` | Runs the test programs without MANA, to check the tests themselves. |
| `--slow N` | Multiplies each timeout by N, for a slow or loaded system. |
| `--retry-once` | Runs a failed test a second time. |
| `--keep` | Keeps the directories of tests that passed. It always keeps the directories of tests that failed. |
| `--workdir DIR` | Puts the test runs in *DIR*. The default is the temporary directory of Python, `tempfile.gettempdir()`. Usually, this is `$TMPDIR` or `/tmp`. |
| `--mana-root DIR` | Tests the MANA installation in *DIR*. |

For all options, type `test/autotest.py --help`.

{: .note }
Run the tests with the default optimization of `./configure`. A debug build
(`--enable-debug`, which uses `-O0`) is much slower, and some tests can time
out.

## How a test works

Most tests are of the kind "loop". The program runs a loop of MPI calls, and
checks each value that it receives. On a wrong value, it calls `MPI_Abort`.
For each test of the kind "loop", `autotest.py` does these steps:

1. It starts a coordinator, and launches the program under MANA.
2. It waits until each rank reports progress.
3. It checkpoints the job, stops it, and restarts it. After each step, it
   waits until each rank reports progress again.
4. It repeats step 3 for each cycle.
5. It tells the program at which iteration to stop. The program must exit
   with status 0 and print that it passed.

A test fails if a rank stops or dies, if a step times out, or if the program
reports an error. The helpers for test programs are in `test/mana_test.h`.
Tests of the kind "run" run to the end by themselves, and must print an
expected line. If the list `TESTS` in `autotest.py` gives a "run" test
`restart=True`, `autotest.py` checkpoints, stops and restarts it once on the
way.

## The tests

The tests in `test/` cover these areas:

| Area | Tests |
|---|---|
| Point-to-point | `p2p_blocking`, `p2p_nonblocking`, `p2p_ring`, `p2p_any_source`, `p2p_probe`, `p2p_large`, `p2p_proc_null` |
| Synchronous sends | `p2p_ssend*`, `p2p_issend*` |
| Collectives | `collectives`, `nonblocking_collectives`, `nbc_order` |
| Communicators | `communicators`, `comm_free`, `comm_free_order`, `comm_self`, `comm_create_group`, `cartesian` |
| Attributes | `attributes`, `keyvals` |
| Datatypes | `datatypes` |
| Memory | `memory`, `realloc_grow`, `shared_memory`, `mmap_threads` |
| Fortran | `fortran_p2p`, `fortran_nonblocking`, `fortran_handles`, `fortran_attributes` |
| Start and end | `init_thread`, `exit_after_finalize`, `finalize_unsync`, `abort`, `dlopen_mpi` |
| Example | `ring_example` (`examples/ring`) |

Two more scripts test parts of MANA without MPI jobs:
`test/test-mana-restart.py` tests the script `mana_restart`, and
`test/test-loader-debug.py` tests the debug lookup of the loader.

## Add a test

1. Write the program in `test/`, for example `test/my_test.c`. Use the
   helpers in `test/mana_test.h`: loop until the harness says stop, and check
   each value that you receive with `MT_CHECK()`. A Fortran test cannot use
   `mana_test.h`. Copy the stop logic of `test/fortran_p2p.f90`.
2. Add the name of the program to `E2E_TESTS` (or `E2E_FORTRAN`) in
   `test/Makefile.in`.
3. Add the test to the list `TESTS` in `test/autotest.py`, with its number of
   ranks.
4. Run `make tests` and `test/autotest.py my_test`. `make` makes
   `test/Makefile` again from `test/Makefile.in` when it changes.
5. Run the test with `--native` too, to check that the program is correct
   without MANA.

## Benchmarks

The benchmarks in `bench/` measure the overhead of MANA. `make tests` builds
them with `-O2`. `make check` does not run them.

`virtid_bench`
: Measures the cost that MANA adds to each wrapped MPI call. Run it without
  MANA and under MANA, and compare the results.

`synthetic_app`
: Runs profiles that are like the communication of some applications: a halo
  exchange, the collectives of VASP, the decomposition of GROMACS, and others.
  A run under MANA must print the same checksum as a run without MANA. See
  `bench/README-synthetic_app.md`.

{: .note }
`synthetic_app` does less computation and more communication than the
applications that inspired its profiles. Thus, the overhead of MANA on a real
application can be different.

## Continuous integration

GitHub Actions runs the workflow `.github/workflows/mana-tests.yml` on each
pull request to `main`. The workflow builds MANA in an Ubuntu 26.04 container
and runs `test/autotest.py --retry-once`, once with MPICH and once with Open
MPI. If the job fails, the workflow keeps the logs of the tests as an
artifact.

The workflow `.github/workflows/website.yml` builds this website for each
pull request to `main` that changes `doc/`. After such a pull request is merged, the
workflow deploys the website to GitHub Pages.

To run the same tests on your computer in a container, start the container
with `--security-opt seccomp=unconfined`, and for MPICH with `--shm-size=2g`
(see [Containers](../install.html#containers)).
