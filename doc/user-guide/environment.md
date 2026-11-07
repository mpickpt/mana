---
title: Environment variables
parent: User guide
nav_order: 4
description: "The environment variables that change how MANA runs."
---

# Environment variables
{: .no_toc }

1. TOC
{:toc}

---

Set these variables before `mana_launch`, unless the description says
otherwise. `mana_launch` gives the environment to all ranks.

## MANA variables

`MANA_P2P_WAIT`
: How `MPI_Send` and `MPI_Recv` wait for their message. The two values are:

  - `polling` (the default): MANA starts the operation as `MPI_Isend` or
    `MPI_Irecv`, then calls `MPI_Test` in a loop until the operation
    completes.
  - `blocking`: MANA calls `MPI_Send` and `MPI_Recv` of the MPI library, which
    wait in the library.

  Both modes checkpoint and restart in the same way. MANA reads the variable at
  `MPI_Init`, and a restarted job keeps the mode of the original job. See
  [Choose a wait mode](#choose-a-wait-mode).

`MANA_PRELOAD`
: A list of libraries, separated by colons, that `mana_launch` loads before
  MANA. A library in this list intercepts MPI calls before MANA does. Use it
  for a PMPI tool, such as a profiler. A library in `LD_PRELOAD` loads after
  these libraries.

`MANA_TIMING`
: If set, rank 0 prints to stderr the time since the start (or the restart),
  at each step of a checkpoint and at exit. `mana_launch --timing` and
  `mana_restart --timing` set this variable.

`MANA_DRAIN_STATS`
: If set, rank 0 prints to stderr the statistics of each checkpoint drain: the
  time of each step (the maximum over all ranks), and the number of messages
  that MANA received for the application. Use it to find why a checkpoint is
  slow.

`MANA_SEGV_DEBUG_LOOP`
: If set to `1`, a rank that gets a segmentation fault loops forever, so that
  you can attach `gdb` to it. See [Debugging](debugging.html).

### Choose a wait mode

The best mode depends on the MPI library and on the placement of the ranks:

- `polling` can be faster when the blocking wait of the MPI library gives the
  processor to other processes (for example, MPICH 5).
- `blocking` is faster with Cray MPICH.
- `blocking` is much faster when two or more ranks share a core.

In `blocking` mode, MANA initializes the MPI library with
`MPI_THREAD_MULTIPLE`. The checkpoint thread then calls MPI while an
application thread waits in `MPI_Recv`. If the library does not supply
`MPI_THREAD_MULTIPLE`, a checkpoint in `blocking` mode can fail (for example,
over UCX).

## DMTCP variables

The variables of DMTCP also apply under MANA. These are the most useful:

`DMTCP_LOG_LEVEL`
: How much DMTCP and MANA print to stderr: `error`, `warn`, `note` or
  `trace`.

`DMTCP_RESTART_PAUSE`
: Stop the restart at a stage from 1 to 7, so that you can attach `gdb`. Set it
  before `mana_restart`. See [Debugging](debugging.html).

`DMTCP_CHECKPOINT_INTERVAL`
: The number of seconds between checkpoints. Set it before
  `mana_coordinator`. It has the same effect as the option `-i`.

`DMTCP_TMPDIR`
: The directory for the temporary files of DMTCP.

For the other DMTCP variables, see the
[DMTCP documentation](https://dmtcp.github.io/).

## Variables that MANA sets

MANA sets these variables itself. Do not set them.

| Variable | Use |
|---|---|
| `MANA_LH_INFO_ADDR` | The address of the data that the lower half gives to the upper half, at launch. |
| `MANA_RESERVED_FDS` | The file descriptors that the lower half keeps free at restart, from `header.mana`. |
| `MANA_FIRST_TIME`, `MANA_STACK_PAD` | Used when the lower half executes itself again, to turn off address space randomization. |
| `LD_AUDIT` | Loads `libmpistub_audit.so` into the upper half. See [Architecture](../developer-guide/architecture.html). |
