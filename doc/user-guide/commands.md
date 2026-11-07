---
title: Commands
parent: User guide
nav_order: 2
description: "The options of mana_coordinator, mana_launch, mana_status and mana_restart."
---

# Commands
{: .no_toc }

1. TOC
{:toc}

---

## Overview

```text
mana_coordinator [--help] [--verbose] [DMTCP_OPTIONS]
mana_launch      [--help] [--verbose] [--timing] [--gdb] [--ckptdir DIR]
                 [DMTCP_OPTIONS] PROGRAM [ARGS...]
mana_status      [--help] [--verbose] [COMMAND]
mana_restart     [--help] [--verbose] [--timing] [--gdb] [--restartdir DIR]
                 [--ckptdir DIR] [DMTCP_OPTIONS]
```

Each MANA command is a wrapper of a DMTCP command:

| MANA command | DMTCP command |
|---|---|
| `mana_coordinator` | `dmtcp_coordinator` |
| `mana_launch` | `dmtcp_launch` |
| `mana_status` | `dmtcp_command` |
| `mana_restart` | The restart code of `dmtcp_restart`, in the MANA lower half |

A MANA command handles the options on this page. It gives all other options to
its DMTCP command, so most DMTCP options work too. MANA does not support
`--enable-unique-checkpoint-filenames` (see
[Plugins and other DMTCP features](checkpoint-restart.html#plugins-and-other-dmtcp-features)).
MANA sets some DMTCP options itself: `--exit-on-last` and `--daemon` for
`dmtcp_coordinator`, and `--join-coordinator` and `--no-gzip` for
`dmtcp_launch`. For the DMTCP options, see the `--help` of each DMTCP command
and the [DMTCP man pages](https://dmtcp.github.io/manpages/dmtcp.html).

The MANA man page, `manpages/mana.1`, has the same information as this page.
To read it in a clone, type `man -l manpages/mana.1`.

## mana_coordinator

Starts a DMTCP coordinator in the background. The coordinator writes its host
and port to a file, where the other commands find them (see
[The coordinator file](#the-coordinator-file)).

`-i SECONDS`, `--interval SECONDS`
: Checkpoint every *SECONDS* seconds. The default is 0: no periodic
  checkpoints.

`--verbose`
: Print the `dmtcp_coordinator` command that `mana_coordinator` runs.

`--help`
: Print the options of `dmtcp_coordinator`.

## mana_launch

Runs *PROGRAM* with *ARGS* under MANA. Run `mana_launch` under the MPI launcher
(for example, `srun` or `mpirun -np N`) in place of *PROGRAM*. A coordinator
must be running.

`--ckptdir DIR`
: Write the checkpoints into *DIR*. The directory must exist. The default is
  the current directory.

`--timing`
: Rank 0 prints to stderr the time since the start, at each step of a
  checkpoint and at exit.

`--verbose`
: Print the `dmtcp_launch` command that `mana_launch` runs. Also print the
  DMTCP notes and warnings, which MANA otherwise hides.

`--gdb`
: Run the `dmtcp_launch` command under `gdb --args`, to debug MANA itself.

`--help`
: Print a usage statement.

`mana_launch` loads the plugins of the DMTCP option `--with-plugin` after the
MANA plugin. To load a library before MANA, use `MANA_PRELOAD` (see
[Environment variables](environment.html)).

`mana_launch` stops with an error in these cases:

- No coordinator is running.
- The checkpoint directory contains `ckpt_rank_*` directories from an earlier
  job.
- *PROGRAM* is not in `PATH` and is not a path to a file.

## mana_status

Without *COMMAND*, lists the ranks that are connected to the coordinator, and
their state. A *COMMAND* asks the coordinator for an action:

`--checkpoint`, `-c`
: Checkpoint the job. The job continues.

`--bcheckpoint`, `-bc`
: Checkpoint the job, and return when the checkpoint is complete.

`--kcheckpoint`, `-kc`
: Checkpoint the job, then stop it.

`-i SECONDS`
: From now on, checkpoint every *SECONDS* seconds. 0 stops the periodic
  checkpoints.

`--verbose`
: Print the `dmtcp_command` command that `mana_status` runs.

`--help`
: Print the usage of `dmtcp_command`, which lists the other commands.

## mana_restart

Restarts a job from its checkpoint. Start a new coordinator first. Then run
`mana_restart` under the MPI launcher, with the same number of ranks as the
job. Each rank N restarts from the directory `ckpt_rank_N`.

`--restartdir DIR`
: Restart from the `ckpt_rank_*` directories in *DIR*. The default is the
  current directory. `mana_restart` stops if a checkpoint in *DIR* is
  incomplete (if it contains a `.temp` file).

`--ckptdir DIR`
: Write later checkpoints into *DIR*. The directory must exist.

`--timing`
: Rank 0 prints to stderr the time since the restart: at the restart, at each
  step of a checkpoint, and at exit.

`--verbose`
: Print the command that `mana_restart` runs. Also print the DMTCP notes and
  warnings, which MANA otherwise hides.

`--gdb`
: Run the restart under `gdb --args`, to debug MANA itself.

`--help`
: Print a usage statement.

## The coordinator file

`mana_coordinator` writes the host and port of the coordinator to a file.
`mana_launch`, `mana_status` and `mana_restart` read the file to find the
coordinator. The name of the file depends on Slurm:

| Condition | File |
|---|---|
| `SLURM_JOB_ID` is not set | `~/.mana.rc` |
| `SLURM_JOB_ID` is set | `~/.mana-slurm-JOBID.rc` |

Outside Slurm, all MANA jobs of a user share `~/.mana.rc`. A second
`mana_coordinator` on another host replaces the file, and the MANA commands
then find the second coordinator. So, outside Slurm, run one MANA job at a
time.

The file has plain text. The lines that the commands read look like this:

```text
Host: node042 (10.0.0.42)
Port: 7779
SLURM_JOB_ID: 4567890
```

The line `SLURM_JOB_ID` is present only in a Slurm job. `mana_launch` and
`mana_restart` delete coordinator files that are more than one week old.

For a job on more than one node, the home directory must be on a file system
that all nodes share. See [Clusters and Slurm](clusters.html).
