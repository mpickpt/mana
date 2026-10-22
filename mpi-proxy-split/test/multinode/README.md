# MPICH/Hydra multi-node checkpoint/restart test

This optional test runs an MPI counter on at least two Linux hosts, checkpoints it with MANA, and verifies its state after restart.

The runner uses an existing MANA and MPICH installation. Without `MANA_TEST_HOSTFILE`, it prints `SKIP` and exits successfully, so ordinary CI jobs do not need access to remote hosts.

## Prerequisites

- Noninteractive SSH access to every host, including the access Hydra needs to launch its remote processes.
- A built MANA runtime at the same absolute path on the controller and all hosts, with matching lower-half and plugin binaries.
- A matching MPICH compiler, library, Hydra launcher, and remote proxy at the same absolute path on all hosts (the reference setup uses MPICH 4.3.2 with CH3/Nemesis and Hydra's internal PMI).
- Loader debug symbols required by the MANA build, and the DMTCP `PMI_FD` socket-scan correction if required by the chosen MPICH/Hydra setup.
- Bash, Python 3.9 or later, coreutils, SSH, and SCP where used by the test.
- A writable work directory at the same absolute path on all hosts. Node-local `/tmp` works; shared storage is optional. Use the same user and UID throughout checkpoint and restart.

Runtime and work-directory paths must be absolute and contain no whitespace or shell metacharacters. IPv6 hostfile entries are unsupported.

## Run

Create a hostfile with one host per line. For the default two ranks, use two hosts:

```text
worker-a
worker-b
```

From the MANA repository, set the installation paths and hostfile:

```bash
export MANA_HOME=/absolute/path/to/built/mana
export MANA_TEST_MPICH_HOME=/absolute/path/to/mpich
export MANA_TEST_HOSTFILE=/absolute/path/to/hosts.txt

bash mpi-proxy-split/test/multinode/run-mpich-hydra.sh
```

Hydra `host:slots` entries are also accepted. Choose a layout that places ranks on at least two hosts. The test checks the application's reported hostnames.

## Configuration

| Variable | Default or requirement |
| --- | --- |
| `MANA_HOME` | Required: built MANA runtime |
| `MANA_TEST_MPICH_HOME` | Required: matching MPICH installation |
| `MANA_TEST_HOSTFILE` | Unset: skip the test |
| `MANA_TEST_REMOTE_USER` | Current user |
| `MANA_TEST_RANKS` | `2` |
| `MANA_TEST_COORD_PORT` | `7780`; must be free on the controller |
| `MANA_TEST_WORKDIR` | `/tmp/mana-multinode-$USER` |
| `MANA_TEST_TIMEOUT` | `300` seconds per bounded operation or progress wait |
| `MANA_TEST_SSH` | `ssh`; an executable name or path |
| `MANA_TEST_SCP` | `scp`; an executable name or path |

The work directory is the parent of a unique directory created for each run.

## What the test checks

1. Remote access, required paths, and matching MANA binaries.
2. Native MPI execution, communicator size, and `MPI_Allreduce` results.
3. A finite MANA run using a directly started DMTCP coordinator.
4. Progress from every rank during a longer MANA run.
5. A blocking checkpoint with one nonempty image and header per rank, no `.temp` or `.tmp` files, stable checkpoint metadata, and no image writers among this run's tagged processes.
6. Restart on the same hosts with the same rank layout. Restart runs from the checkpoint directory, with `--restartdir` after the other options.
7. Rollback into the checkpointed interval, followed by progress beyond the pre-termination state and correct collective results from every rank.

The application advances after checkpointing so the test can observe rollback.

## Cleanup and logs

Each run uses a unique directory and a private `HOME` and `.mana.rc`.
Cleanup stops the coordinator started by the runner and processes owned by the current user that carry the exact run tag.

The runner prints the evidence directory when it exits.
Logs and checkpoint files remain available for inspection (remove them manually when finished).
With node-local storage, inspect and remove the run directory on each host.

## Checks without a cluster

These commands check shell syntax and the default skip behavior (they do not validate distributed checkpoint/restart):

```bash
bash -n mpi-proxy-split/test/multinode/run-mpich-hydra.sh
env -u MANA_TEST_HOSTFILE \
  bash mpi-proxy-split/test/multinode/run-mpich-hydra.sh
```

