---
date: 31 March 2022
section: 1
title: MANA
description: MPI-Agnostic Network-Agnostic Checkpointing
---

# NAME

**mana** -- MANA family of commands for checkpointing MPI jobs

# SYNOPSIS

**mana_launch** [\--help] [\--verbose] [\--timing] [DMTCP_OPTIONS] [\--ckptdir *DIR*] COMMAND [*ARGS...*]\
**mana_coordinator** [\--help] [\--verbose] [*DMTCP_OPTIONS*]\
**mana_start_coordinator**\
**mana_status** [\--help] [\--verbose] [*DMTCP_OPTIONS*]\
**mana_restart** [\--help] [\--verbose] [*DMTCP_OPTIONS*] [\--restartdir *DIR*]

# DESCRIPTION

**MANA** is a package that enables checkpoint-restart for MPI jobs. The
name MANA stands for "MPI-Agnostic Network-Agnostic
Checkpointing". It is designed to be compatible with most MPI
implementations and most underlying networks. MANA is built on top of
the DMTCP checkpointing package.

# COMMAND LINE OPTIONS

**`--help`**
: Show additional DMTCP_OPTIONS for a MANA command.

**`--verbose`**
: Display the underlying DMTCP command and DMTCP_OPTIONS used, and other info.

**`--timing`**
: Print times to stderr for INIT, EXIT, and ckkpt-restart events.
  (stays active during both mana_launch and mana_restart)

# MANA PROGRAM EXECUTION

Execute a MANA command with `--help` for a usage statement.

Checkpoints may be invoked:

**`periodically`**
: via the `--interval` or `-i` flag (units in seconds)

**`under program control`** 
: (see `dmtcp/test/plugin/applic-initiated-ckpt` directory)

**`externally`**
: via `mana_status --checkpoint`

A typical workflow for using MANA after untar\'ing is:

```bash
cd dmtcp-mana
./configure --enable-debug
make -j mana
# Build the program with mpicc as usual; no relinking for MANA is needed.
# 'make' also builds an example: mpi-proxy-split/examples/ring
salloc -N 2 -q interactive -C haswell -t 01:00:00
bin/mana_coordinator -i10
srun -N 2 bin/mana_launch mpi-proxy-split/examples/ring
bin/mana_coordinator -i10
srun -N 2 bin/mana_restart
```

MANA supports most features of DMTCP, including:

* plugins to extend the functionality of DMTCP
* virtualization of pathname prefixes (plugin/pathvirt)
* modification of environment variables at restart (plugin/modify-env)
* distinct checkpoint filenames (`dmtcp_launch --enable-unique-checkpoint-filenames`). The default is to overwrite the last checkpoint.

# ENVIRONMENT VARIABLES AND DEBUGGING

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

**`MANA_DEBUG`**

: MANA will print to stderr extra information to help developers debug MANA.

**`MANA_PRELOAD`**

: A colon-separated list of libraries that `mana_launch` loads ahead of MANA,
  so that they intercept MPI calls before MANA does (e.g., a PMPI profiler).
  A library in `LD_PRELOAD` loads after them.

**`DMTCP_MANA_PAUSE` or `DMTCP_LAUNCH_PAUSE`**

: DMTCP/MANA will pause during launch to allow `gdb attach` (GDB must be on same node.)

**`DMTCP_MANA_PAUSE`**

: DMTCP/MANA will pause very early in restart, inside
  `mtcp/mtcp_restart.c` shortly before calling `splitProcess()` (intended
  for developers)

**`DMTCP_RESTART_PAUSE`**

: DMTCP/MANA will pause during restart, before resuming execution, to
  allow `gdb attach` (GDB must be on same node.)

**`MPI_COLLECTIVE_P2P`**

: **For debugging only (high runtime overhead)**: If this env. var. is
  set, and if re-building any files in
  `mpi-proxy-split/mpi-wrappers`, then MPI collective
  communication calls are translated to MPI_Send/Recv at runtime. (Try
  `touch mpi_collective_p2p.c` if not re-building.)\

  NOTE: You can select specific collective calls for translation to
  MPI_Send/Recv by copying MPI wrappers from mpi_collective_p2p.c to
  mpi_collective_wrappers.cpp in the mpi-wrappers subdirectory; or
  block certain translations by adjusting `#ifdef/#ifndef MPI_COLLECTIVE_P2P` in those files.

**`INSPECTING MANA for DEBUGGING`**

To see status of ranks (especially during checkpoint), try:

    bin/mana_status --list

To inspect a checkpoint image from, for example, Rank `0`, try:

    util/readdmtcp.sh ckpt_rank0/ckpt_*.dmtcp

To debug during restart at stage 'N', replace 'N' Below, and execute:

    srun ... env DMTCP\_RESTART\_PAUSE=N mana\_restart ... APPLICATION &
    gdb -p APPLICATION PID  # from a different terminal

To see the stack, you then may or may not need to try some of the
following:

    (gdb) source util/gdb-dmtcp-utils
    (gdb) load-symbol-library ADDRESS-OR-LIBRARY-SUBSTRING
    (gdb) dmtcp  # to see other available commands

If you are debugging the lower half internals of MANA, you may need:

    (gdb) file bin/lh_proxy

If debugging the low-level restart process, please read restart/plugin/README.

# BUGS

Report bugs in MANA to: https://github.com/mpickpt/mana

# SEE ALSO

**MANA home page:** \<https://github.com/mpickpt/mana\>, \
**dmtcp**(1), **dmtcp_coordinator**(1), **dmtcp_launch**(1), \
**dmtcp_restart**(1), **dmtcp_command**(1)
