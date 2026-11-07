---
title: Checkpoint and restart
parent: User guide
nav_order: 1
description: "The ways to checkpoint an MPI job with MANA, the checkpoint files, and restart."
---

# Checkpoint and restart
{: .no_toc }

1. TOC
{:toc}

---

## The steps of a MANA job

A MANA job uses four commands:

1. `mana_coordinator` starts a coordinator. The coordinator controls the
   checkpoints of one job.
2. `mana_launch` runs each rank of the MPI program under MANA. Run it under
   the MPI launcher, for example `mpirun -np 4` or `srun`.
3. `mana_status` lists the ranks, and asks the coordinator for a checkpoint.
4. `mana_restart` restarts the ranks from their checkpoint. Run it under the
   MPI launcher, with the same number of ranks as the job.

For example, with the example program `examples/ring` on one host:

```bash
bin/mana_coordinator                         # 1. Start a coordinator.
mpirun -np 4 bin/mana_launch examples/ring   # 2. Run the program under MANA.
bin/mana_status --bcheckpoint                # 3. In another shell: checkpoint.
```

To stop the job and restart it from the checkpoint:

```bash
# 4. Stop the job, for example with Ctrl-C.
bin/mana_coordinator                         # 5. Start a new coordinator.
mpirun -np 4 bin/mana_restart                # 6. Restart from ckpt_rank_*.
```

A coordinator stops when the last rank of its job stops. So each run needs a
new coordinator.

## Ways to checkpoint

| Command | Result |
|---|---|
| `mana_status --checkpoint` (or `-c`) | Checkpoints the job. The job continues. |
| `mana_status --bcheckpoint` (or `-bc`) | Checkpoints the job. The command returns when the checkpoint is complete. |
| `mana_status --kcheckpoint` (or `-kc`) | Checkpoints the job, then stops it. |
| `mana_coordinator -i SECONDS` | Checkpoints the job every *SECONDS* seconds. |
| `mana_status -i SECONDS` | Sets the checkpoint interval of a running job. 0 stops the periodic checkpoints. |

A program can also ask for a checkpoint itself, with the DMTCP API. For an
example, see `dmtcp/test/plugin/applic-initiated-ckpt` in the MANA source tree.

{: .note }
`mana_status --bcheckpoint` returns before the ranks run again. Wait until
`mana_status` shows the ranks as `RUNNING` before you ask for the next
checkpoint. Otherwise, the coordinator refuses the request.

## The checkpoint files

MANA writes the checkpoint of rank N into the directory `ckpt_rank_N`. By
default, `ckpt_rank_N` is in the current directory. The option
`mana_launch --ckptdir DIR` puts it in *DIR*. Each `ckpt_rank_N` contains these
files:

| File | Contents |
|---|---|
| `ckpt_*.dmtcp` | The checkpoint image of the rank, written by DMTCP. |
| `header.mana` | Data that `mana_restart` reads: the `MANA_P2P_WAIT` mode of the job, and the file descriptors that were open. |
| `mpi_files.mana` | The MPI files that were open, if the program used MPI-IO. |

DMTCP writes each image to a file with the suffix `.temp`, and renames it when
the image is complete. `mana_restart` does not start if a `.temp` file is
present, because the checkpoint can be incomplete.

A new checkpoint replaces the previous one. To keep an older checkpoint, copy
the `ckpt_rank_*` directories to a different location. Copy them after the
checkpoint is complete, and before the next checkpoint starts.

`mana_launch` does not start if the checkpoint directory already contains
`ckpt_rank_*` directories. This prevents a new job from replacing an old
checkpoint by accident. Move or delete the old checkpoint before a new job.

## Restart

`mana_restart` restarts each rank N from the directory `ckpt_rank_N`. Start a
new coordinator first:

```bash
bin/mana_coordinator
mpirun -np 4 bin/mana_restart --restartdir DIR
```

Obey these rules for a restart:

- Use the same number of ranks as the original job.
- Use the same MPI library, the same MANA build, and the same network as the
  original job. The ranks can restart on other nodes.
- The program and the files that it had open must be at the same paths as
  before.

The restarted job uses the `MANA_P2P_WAIT` mode of the original job (see
[Environment variables](environment.html)). A restarted job can checkpoint
and restart again. To write its later checkpoints into a different directory,
use `mana_restart --ckptdir DIR`.

## Plugins and other DMTCP features

MANA supports most features of DMTCP. For example:

- `mana_launch --with-plugin PLUGIN.so` loads a DMTCP plugin after MANA.
- The DMTCP plugin `modify-env` changes environment variables at restart. See
  `dmtcp/plugin/modify-env` in the MANA source tree.

MANA does not support the DMTCP option `--enable-unique-checkpoint-filenames`.
`mana_restart` takes one image from each `ckpt_rank_N` directory, and that
image is not always the newest one.
