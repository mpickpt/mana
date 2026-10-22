# Opt-in MPICH/Hydra multi-node test

This small test uses an already prepared MANA/MPICH runtime on at least two
SSH-accessible Linux nodes. It does not install packages, configure SSH,
replace workers, or run the v43 failover experiment.

Without `MANA_TEST_HOSTFILE`, the runner prints `SKIP` and exits successfully.
It is not a mandatory distributed job on ordinary CI runners.

## Prerequisites

- Passwordless, noninteractive SSH from the controller to every host and
  from Hydra's launcher as required by the installation.
- MANA and MPICH at identical absolute paths on all hosts, with matching
  MANA lower-half and plugin binaries. The controller has the same runtime.
- A matching MPICH compiler, library, Hydra launcher, and remote proxy. The
  working reference is MPICH 4.3.2, CH3/Nemesis, with Hydra/internal PMI.
  Some Ubuntu MPICH packages were built with external PMIx and do not work
  with Hydra; native MPI must pass before the MANA stages.
- The debug symbols needed by the chosen MANA build. The known-working
  v1.5.0 setup uses a loader debug compatibility link; PR #488 proposes an
  alternative lookup. This test does not depend on that unmerged PR.
- The Hydra `PMI_FD` socket correction where required by the runtime. It was
  submitted upstream in DMTCP PR #1310; a MANA build must actually contain
  the fix, rather than merely referring to the merged DMTCP PR.
- Bash, Python 3.9 or later, coreutils, SSH, and SCP on the relevant hosts.
- A writable work-root path, accessible under the same absolute name on all
  nodes. Node-local `/tmp` is supported; shared storage is optional. Use the
  same user/UID for the test and restart. This test does not bypass DMTCP's
  checkpoint ownership checks.

Run this SSH/Hydra test outside a Slurm launch environment. Paths containing
whitespace or shell metacharacters and IPv6 hostfile entries are unsupported.

## Run

Create a hostfile with one host per line. With the default two ranks, use
two hosts so both are exercised:

```text
worker-a
worker-b
```

Then run from the MANA repository:

```bash
export MANA_HOME=/absolute/path/to/built/mana
export MANA_TEST_MPICH_HOME=/absolute/path/to/mpich-4.3.2
export MANA_TEST_HOSTFILE=/absolute/path/to/hosts.txt

bash mpi-proxy-split/test/multinode/run-mpich-hydra.sh
```

Hydra `host:slots` entries are also accepted. Choose a layout that places
ranks on at least two real hosts; the test verifies the hostnames reported
by the application.

## Configuration

| Variable | Default |
| --- | --- |
| `MANA_HOME` | Required, prebuilt MANA runtime |
| `MANA_TEST_MPICH_HOME` | Required, matching MPICH installation |
| `MANA_TEST_HOSTFILE` | Unset; the test skips |
| `MANA_TEST_REMOTE_USER` | Current user |
| `MANA_TEST_RANKS` | `2` |
| `MANA_TEST_COORD_PORT` | `7780`, unused on the controller |
| `MANA_TEST_WORKDIR` | `/tmp/mana-multinode-$USER`, parent of unique runs |
| `MANA_TEST_TIMEOUT` | `300` seconds per bounded operation/progress wait |
| `MANA_TEST_SSH` | `ssh`, an executable path |
| `MANA_TEST_SCP` | `scp`, an executable path |

## What it checks

1. Connectivity, required paths, and matching MANA binaries.
2. Native distributed MPI, including communicator size and every rank's
   `MPI_Allreduce` result.
3. Direct DMTCP coordinator startup and a finite MANA smoke test.
4. A longer MANA run with progress from every rank on at least two hosts.
5. A blocking checkpoint, followed by exactly one nonempty image and header
   per rank, no `.temp`/`.tmp` files, stable metadata, and no image writers
   among this run's tagged processes.
6. Termination and restart with the same rank layout. The runner retains the
   released wrapper's checkpoint-directory/option-order workaround so the
   test remains independent of the restart-wrapper PR.
7. Per-rank rewind into the checkpointed interval, followed by progress past
   the pre-termination state and correct collective results. A fresh launch
   starting at iteration zero cannot satisfy the saved lower bound.

The script uses `ssh -n`, a private `HOME`/`.mana.rc`, a unique run directory,
and an exact environment tag for process cleanup. It uses the existing
`dmtcp_coordinator` directly, consistent with the resolution of MANA #487.
Cleanup stops its owned coordinator and tagged processes; it does not match
arbitrary processes by command-line substring. Logs and checkpoints remain
in the printed evidence directory for inspection and explicit removal.

## Local checks

These do not establish that distributed execution works:

```bash
bash -n mpi-proxy-split/test/multinode/run-mpich-hydra.sh
env -u MANA_TEST_HOSTFILE \
  bash mpi-proxy-split/test/multinode/run-mpich-hydra.sh
```

Report a new multi-node run's source/runtime revisions, node/rank layout,
output, and final `PASS` separately from earlier v43 experiment results.
