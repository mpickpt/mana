# Opt-in MPICH/Hydra multi-node test

This test is not run unless `MANA_TEST_HOSTFILE` is set.

## Required variables

- `MANA_HOME`: configured and built MANA source/install tree available at the same absolute path on every node.
- `MANA_TEST_MPICH_HOME`: MPICH installation available at the same absolute path on every node.
- `MANA_TEST_HOSTFILE`: Hydra hostfile or a plain file containing one host per line.

## Optional variables

- `MANA_TEST_REMOTE_USER` (default: current user)
- `MANA_TEST_RANKS` (default: 2)
- `MANA_TEST_COORD_PORT` (default: 7780)
- `MANA_TEST_WORKDIR` (default: `/tmp/mana-multinode-$USER`)
- `MANA_TEST_TIMEOUT` (default: 300 seconds)
- `MANA_TEST_SSH` (default: `ssh`)
- `MANA_TEST_SCP` (default: `scp`)

## What it validates

1. Remote connectivity and identical executable paths.
2. Native MPICH/Hydra execution.
3. Finite MANA launch.
4. Persistent DMTCP coordinator behavior.
5. Long-running distributed MANA launch.
6. Blocking checkpoint with timeout.
7. Non-empty images and absence of `.tmp` files.
8. Kill and restart.
9. Rollback and post-restart `MPI_Allreduce` correctness.

The script creates and removes only files under `MANA_TEST_WORKDIR` and processes whose command line contains that path.
