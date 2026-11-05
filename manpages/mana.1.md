---
date: 5 November 2026
section: 1
title: MANA
description: MPI-Agnostic Network-Agnostic Checkpointing
---

# NAME

**mana** -- MANA family of commands for checkpointing MPI jobs

# SYNOPSIS

**mana_coordinator** [\--help] [\--verbose] [*DMTCP_OPTIONS*]\
**mana_launch** [\--help] [\--verbose] [\--timing] [\--gdb] [\--ckptdir *DIR*] [*DMTCP_OPTIONS*] *PROGRAM* [*ARGS...*]\
**mana_status** [\--help] [\--verbose] [*COMMAND*]\
**mana_restart** [\--help] [\--verbose] [\--timing] [\--gdb] [\--restartdir *DIR*] [\--ckptdir *DIR*] [*DMTCP_OPTIONS*]

# DESCRIPTION

**MANA** is a package that enables checkpoint-restart for MPI jobs. The
name MANA stands for "MPI-Agnostic Network-Agnostic
Checkpointing". It is designed to be compatible with most MPI
implementations and most underlying networks. MANA is built on top of
the DMTCP checkpointing package.

A MANA job uses four commands. `mana_coordinator` starts the coordinator
that the ranks connect to. `mana_launch` runs each rank of an MPI program
under MANA. `mana_status` lists the ranks and asks the coordinator for a
checkpoint. `mana_restart` restarts the ranks from their checkpoint. The MPI
program needs no change and no relinking. The four commands are wrappers of
DMTCP's commands, and they accept DMTCP's options too (see COMMANDS AND
OPTIONS).

MANA's website, \<https://mpickpt.github.io/mana/\>, has the rest of MANA's
documentation: a user guide, a developer guide and the release notes.

# CHECKPOINT AND RESTART

For example, with the example program `examples/ring` (see BUILDING) on one
host, from MANA's directory:

```bash
bin/mana_coordinator                         # 1. Start a coordinator.
mpirun -np 4 bin/mana_launch examples/ring   # 2. Run the program under MANA.
bin/mana_status --bcheckpoint                # 3. In another shell: checkpoint.
```

At step 3, each rank N writes its checkpoint into the directory
`ckpt_rank_N` in the current directory. The command returns when the
checkpoint is complete, and the program goes on. To stop the job and
restart it from the checkpoint:

```bash
# 4. Stop the job, for example with Ctrl-C.
bin/mana_coordinator                         # 5. Start a new coordinator.
mpirun -np 4 bin/mana_restart                # 6. Restart from ckpt_rank_*.
```

The restarted program goes on from where the checkpoint was taken. Restart
with the same number of ranks. A coordinator exits when its last rank
exits, so each run needs a new one.

Other ways to checkpoint:

* `bin/mana_status --kcheckpoint`: checkpoint, then stop the job (steps 3
  and 4 in one command).
* `bin/mana_status --checkpoint`: checkpoint, and return at once, before
  the checkpoint is done.
* `bin/mana_coordinator -i SECONDS`: checkpoint every *SECONDS* seconds.
* From the program, with DMTCP's API (see the directory
  `dmtcp/test/plugin/applic-initiated-ckpt`).

A new checkpoint replaces the previous one. `mana_launch` stops if the
checkpoint directory already has `ckpt_rank_*` directories: move or delete
an old checkpoint before a new run.

With Slurm, run the same commands in an allocation, with `srun`:

```bash
salloc -N 2 -t 01:00:00  # or your cluster's command for an allocation
bin/mana_coordinator -i 60
srun -N 2 bin/mana_launch examples/ring
bin/mana_coordinator -i 60
srun -N 2 bin/mana_restart
```

# COMMANDS AND OPTIONS

Each MANA command is a wrapper of a DMTCP command: `mana_coordinator` runs
`dmtcp_coordinator`, `mana_launch` runs `dmtcp_launch`, `mana_status` runs
`dmtcp_command`, and `mana_restart` runs the restart of `dmtcp_restart` in
MANA's lower half. A MANA command handles the options below, and passes the
others to its DMTCP command, so DMTCP's options work too. MANA sets some of
them itself: `--exit-on-last` and `--daemon` for `dmtcp_coordinator`, and
`--join-coordinator` and `--no-gzip` for `dmtcp_launch`. For DMTCP's
options, see each DMTCP command's `--help`, DMTCP's man pages (see SEE
ALSO), and DMTCP's documentation at \<https://dmtcp.github.io/\>.

## mana_coordinator

Starts a DMTCP coordinator in the background, and writes its host and port
to a file where the other commands find it (see FILES).

**`-i SECONDS`**, **`--interval SECONDS`**
: Checkpoint every *SECONDS* seconds (default: 0, no periodic checkpoints).

**`--verbose`**
: Print the `dmtcp_coordinator` command that `mana_coordinator` runs.

**`--help`**
: Print the options of `dmtcp_coordinator`.

## mana_launch

Runs *PROGRAM* with *ARGS* under MANA. Run `mana_launch` under the MPI
launcher, such as `srun` or `mpirun -np N`, in place of *PROGRAM*. A
coordinator must be running.

**`--ckptdir DIR`**
: Write the checkpoints into *DIR*, which must exist (default: the current
  directory).

**`--timing`**
: Rank 0 prints to stderr the time since the start at each step of a
  checkpoint, and at exit.

**`--verbose`**
: Print the `dmtcp_launch` command that `mana_launch` runs, and DMTCP's notes
  and warnings, which are otherwise not printed.

**`--gdb`**
: Run the `dmtcp_launch` command under `gdb --args`, to debug MANA itself.

**`--help`**
: Print a usage statement.

`mana_launch` loads the plugins of DMTCP's `--with-plugin` after MANA's own.

## mana_status

Without *COMMAND*, lists the ranks that are connected to the coordinator. A
*COMMAND* asks the coordinator for an action:

**`--checkpoint`**, **`-c`**
: Checkpoint the job. The job goes on.

**`--bcheckpoint`**, **`-bc`**
: Checkpoint the job, and return when the checkpoint is done.

**`--kcheckpoint`**, **`-kc`**
: Checkpoint the job, then stop it.

**`-i SECONDS`**
: Checkpoint every *SECONDS* seconds from now on (0: no periodic
  checkpoints).

**`--verbose`**
: Print the `dmtcp_command` command that `mana_status` runs.

**`--help`**
: Print the usage of `dmtcp_command`, which lists the other commands.

## mana_restart

Restarts a job from its checkpoint. Run `mana_restart` under the MPI
launcher, with the same number of ranks as the job, after a new
`mana_coordinator`. Each rank N restarts from `ckpt_rank_N`, and keeps the
`MANA_P2P_WAIT` mode of the job.

**`--restartdir DIR`**
: Restart from the `ckpt_rank_*` directories in *DIR* (default: the current
  directory). `mana_restart` stops if a checkpoint there is incomplete (a
  `.temp` file).

**`--ckptdir DIR`**
: Write later checkpoints into *DIR*, which must exist.

**`--timing`**
: Rank 0 prints to stderr the time since the restart at the restart, at
  each step of a checkpoint, and at exit.

**`--verbose`**
: Print the command that `mana_restart` runs, and DMTCP's notes and
  warnings, which are otherwise not printed.

**`--gdb`**
: Run the restart under `gdb --args`, to debug MANA itself.

**`--help`**
: Print a usage statement.

# BUILDING

```bash
git clone https://github.com/mpickpt/mana
cd mana
./configure
make -j
make tests  # the tests, and the example examples/ring
```

`make` puts MANA's commands in `bin/`. To install MANA into another
directory, run `./configure --prefix=DIR`, `make` and `make install`. The
commands are then in `DIR/bin`.

# NOTES

MANA supports most features of DMTCP, including plugins
(`mana_launch --with-plugin`) and the modification of environment variables
at restart (`dmtcp/plugin/modify-env`).

MANA does not support DMTCP's distinct checkpoint filenames
(`--enable-unique-checkpoint-filenames`): `mana_restart` takes one image from
each `ckpt_rank_N` directory, and not always the newest.

# ENVIRONMENT

**`MANA_P2P_WAIT`**

: How `MPI_Send` and `MPI_Recv` wait for their message.  `polling` (the
  default): as `MPI_Isend` or `MPI_Irecv`, then a loop of `MPI_Test` in MANA.
  `blocking`: in the MPI library's own `MPI_Send` and `MPI_Recv`.  Both modes
  checkpoint and restart the same way.  `polling` can be faster when the MPI
  library's blocking wait yields the processor (MPICH 5).  `blocking` is
  faster on Cray MPICH, and much faster when two ranks share a core.  In
  `blocking` mode, MANA initializes the MPI library with
  `MPI_THREAD_MULTIPLE`; with a library that does not provide it, `blocking`
  can fail at a checkpoint (e.g., over UCX).  MANA reads the variable at
  `MPI_Init` and keeps the mode after restart.

**`MANA_PRELOAD`**

: A colon-separated list of libraries that `mana_launch` loads ahead of MANA,
  so that they intercept MPI calls before MANA does (e.g., a PMPI profiler).
  A library in `LD_PRELOAD` loads after them.

**`DMTCP_LOG_LEVEL`**

: How much DMTCP prints to stderr: `error`, `warn`, `note` or `trace`.

**`DMTCP_RESTART_PAUSE`**

: DMTCP/MANA will pause during restart, at the stage given by the value (1
  to 7), to allow `gdb attach` (GDB must be on same node.)

# DEBUGGING

To see status of ranks (especially during checkpoint), try:

    bin/mana_status

To inspect a checkpoint image from, for example, Rank `0`, try:

    dmtcp/util/readdmtcp.sh ckpt_rank_0/ckpt_*.dmtcp

To debug during restart at stage 'N', replace 'N' below, and execute:

    srun ... env DMTCP_RESTART_PAUSE=N bin/mana_restart &
    gdb -p PID  # PID of the paused rank, from another terminal

To see the stack, you then may or may not need to try some of the
following:

    (gdb) source dmtcp/util/gdb-dmtcp-utils.py
    (gdb) load-symbols-library ADDRESS-OR-LIBRARY-SUBSTRING
    (gdb) dmtcp  # to see other available commands

If you are debugging the lower half internals of MANA, you may need:

    (gdb) file bin/lower-half

If debugging the low-level restart process, please read src/lower-half/README.

# FILES

**`~/.mana.rc`**, or **`~/.mana-slurm-JOBID.rc`** in a Slurm job

: The coordinator's host and port, written by `mana_coordinator` and read by
  the other commands. `mana_launch` and `mana_restart` delete these files
  when they are more than a week old.

**`ckpt_rank_N/`**

: The checkpoint of rank N, in the checkpoint directory: its image
  `ckpt_*.dmtcp` and the file `header.mana`.

# BUGS

Report bugs in MANA to: https://github.com/mpickpt/mana

# SEE ALSO

**MANA website:** \<https://mpickpt.github.io/mana/\>, \
**MANA source code:** \<https://github.com/mpickpt/mana\>, \
**DMTCP home page:** \<https://dmtcp.github.io/\>, \
**dmtcp**(1), **dmtcp_coordinator**(1), **dmtcp_launch**(1), \
**dmtcp_restart**(1), **dmtcp_command**(1)
