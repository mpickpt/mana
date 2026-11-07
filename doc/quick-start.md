---
title: Quick start
nav_order: 2
description: "Build MANA, run an example under MANA, checkpoint it and restart it."
---

# Quick start
{: .no_toc }

This page builds MANA, runs an example program under MANA on one host,
checkpoints it, and restarts it. It takes about ten minutes.
{: .fs-6 .fw-300 }

1. TOC
{:toc}

---

## Before you start

You need an MPI library with its compiler wrappers (`mpicc`, `mpicxx` and
`mpifort`) in `PATH`, a C++20 compiler, and Python 3. On Debian and Ubuntu,
also install the package `libc6-dbg`. [Install](install.html) gives the full
list of requirements.

## 1. Build MANA

```bash
git clone https://github.com/mpickpt/mana
cd mana
./configure      # also checks out DMTCP, the dmtcp submodule of MANA
make -j8
```

`make` puts the MANA commands in `bin/` and the libraries in `lib/dmtcp/`. Run
the commands below from the top directory of MANA.

## 2. Build the example program

```bash
make -C examples
```

This builds `examples/ring`. In each step, the program passes a token around a
ring of all ranks and sums one value per rank. It runs 60 steps of one second
each, and rank 0 prints one line per step. The options `-n STEPS` and
`-s SECONDS` change these values.

## 3. Start a coordinator

```bash
bin/mana_coordinator
```

The coordinator controls the checkpoints of a job. It runs in the background.
It writes its host and port to `~/.mana.rc`, where the other MANA commands
find them.

## 4. Run the program under MANA

```bash
mpirun -np 4 bin/mana_launch examples/ring
```

`mana_launch` runs each rank under MANA. Give `mana_launch` to the MPI launcher
in place of the program, and give the program to `mana_launch`.

## 5. Checkpoint the job

While the job runs, open another shell in the same directory, and type:

```bash
bin/mana_status --bcheckpoint
```

Each rank N writes its checkpoint into the directory `ckpt_rank_N` in the
current directory. The command returns when the checkpoint is complete. Then
the job continues.

## 6. Stop the job

Stop the job with Ctrl-C in the first shell.

## 7. Restart the job

A coordinator stops when the last rank of its job stops. So, start a new
coordinator, and then restart the job:

```bash
bin/mana_coordinator
mpirun -np 4 bin/mana_restart
```

The job continues from the step of the checkpoint, not from step 1. Restart
with the same number of ranks as the original job.

{: .note }
`mana_launch` does not start if the checkpoint directory already contains
`ckpt_rank_*` directories. Move or delete an old checkpoint before you launch
a new job. `make tidy` deletes the checkpoints in the top directory of MANA.

## On a cluster with Slurm

Use the same commands in an allocation, and use `srun` in place of `mpirun`:

```bash
salloc -N 2 -t 01:00:00
bin/mana_coordinator
srun -N 2 bin/mana_launch examples/ring
```

If `salloc` leaves your shell on a login node that the compute nodes cannot
reach, start the coordinator on a node of the allocation.
[Clusters and Slurm](user-guide/clusters.html) gives more information.

## Next steps

- [Checkpoint and restart](user-guide/checkpoint-restart.html): the other ways
  to checkpoint, for example periodic checkpoints.
- [Commands](user-guide/commands.html): the options of each MANA command.
- [MPI support and limitations](user-guide/mpi-support.html): what MANA
  supports.
