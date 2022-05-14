---
date: 31 March 2022
section: 1
title: MANA
description: MPI-Agnostic Network-Agnostic Checkpointing
---

# NAME

**mana** -- MANA family of commands for checkpointing MPI jobs

# SYNOPSIS

**mana_launch** [\--help] [\--verbose] [DMTCP_OPTIONS] [\--ckptdir *DIR*] COMMAND [*ARGS...*]\
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
# Compile against libmana.so: Examples at contrib/mpi-proxy-split/test
salloc -N 2 -q interactive -C haswell -t 01:00:00
bin/mana_coordinator -i10
srun -N 2 bin/mana_launch <TARGET_DIR>/ping_pong.mana.exe
bin/mana_coordinator -i10
srun -N 2 bin/mana_restart
```

MANA supports most features of DMTCP, including:

* plugins to extend the functionality of DMTCP
* virtualization of pathname prefixes (plugin/pathvirt)
* modification of environment variables at restart (plugin/modify-env)
* distinct checkpoint filenames (`dmtcp_launch --enable-unique-checkpoint-filenames`). The default is to overwrite the last checkpoint.

# ENVIRONMENT VARIABLES AND DEBUGGING

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

**`MANA_P2P_LOG`\"**

: For debugging: Set this before mana_launch in order to log the
  order of point-to-point calls (MPI_Send and family) for later
  deterministic replay. See details at top of
  `mpi-proxy-split/mpi-wrappers/p2p-deterministic.c`.

  (IMPORTANT: If you checkpoint, continue running for a few minutes after that,
  for final updating of the log files.)

**`MANA_P2P_REPLAY`**

: For debugging: If a checkpoint was created with `MANA_P2P_LOG`, then
  execute `mana_p2p_update_logs` and set this variable before
  `mana_restart`. (Currently, you need to set this before `mana_launch`,
  but this may be fixed later.)

To see status of ranks (especially during checkpoint), try:

    bin/mana_status --list

To inspect a checkpoint image from, for example, Rank `0`, try:

    util/readdmtcp.sh ckpt_rank0/ckpt_*.dmtcp

To debug during restart, try:

    srun ... env DMTCP\_RESTART\_PAUSE=1 mana\_restart ... APPLICATION
    gdb APPLICATION PID  # from a different terminal

To see the stack, you then may or may not need to try some of the
following:

    (gdb) add-symbol-file bin/lh_proxy
    (gdb) source util/gdb-dmtcp-utils
    (gdb) add-symbol-files-all
    (gdb) dmtcp

If you are debugging the lower half internals of MANA, you may need:

    (gdb) file bin/lh_proxy

# BUGS

NOTE: Compiling VASP-5.4.4 uncovered some bugs in the combination of
`icpc-19.0.3.199` and GNU `gcc`.

The validation results were:

* **icpc+gcc-7.5.0 (built at NERSC)**: Fails at runtime on VASP
  `during restart` unless configured with:

      ./configure CXXFLAGS='-fno-omit-frame-pointer -fno-optimize-sibling-calls'

* **icpc+gcc-9.3.0**: Works correctly

* **icpc_gcc-10.1.0 (built at NERSC)**: Fails to link using ld-2.35.1

Report bugs in MANA to: https://github.com/mpickpt/mana

# SEE ALSO

**dmtcp**(1), **dmtcp_coordinator**(1), **dmtcp_launch**(1),
**dmtcp_restart**(1), **dmtcp_command**(1)\
**MANA home page:** \<https://github.com/mpickpt/mana\>
