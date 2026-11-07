---
title: Debugging
parent: User guide
nav_order: 6
description: "What to do when a MANA job, a checkpoint or a restart fails."
---

# Debugging
{: .no_toc }

1. TOC
{:toc}

---

The DMTCP documentation covers the debugging topics that DMTCP and MANA share:
debug builds, the `gdb` utilities of DMTCP, and the pauses at restart. This
page covers the topics that are specific to MANA.

## Find the smallest case that fails

Before you use `gdb`, find the smallest configuration that shows the problem.
Do these steps in sequence, and stop at the first step that fails:

1. Run the program without MANA, with `mpirun` or `srun`.
2. Run it under MANA with one rank.
3. Run it under MANA with two or more ranks on one node.
4. Run it under MANA with two ranks on two nodes.
5. Run it under MANA at full scale.

For steps 2 to 5, do a checkpoint and a restart too. Then debug the first step
that fails.

## A checkpoint does not complete

First, look at the state of the ranks:

```bash
bin/mana_status
```

`mana_status` lists each rank and its state. A rank that stays `RUNNING` while
the others are `SUSPENDED` did not reach the checkpoint. Usually, that rank is
in a long computation, or in an MPI call that waits for another rank. Before
a checkpoint, MANA lets each collective call that started complete on all
ranks of its communicator. So a rank that is late to a collective delays the
checkpoint.

To see where the time goes in a checkpoint, set `MANA_DRAIN_STATS=1` (or use
`mana_launch --timing`) before `mana_launch`. Rank 0 then prints the time of
each step of the checkpoint.

## Read the messages of DMTCP

By default, MANA hides the notes and warnings of DMTCP. To see them, use
`mana_launch --verbose` or `mana_restart --verbose`, or set
`DMTCP_LOG_LEVEL=note`. `DMTCP_LOG_LEVEL=trace` also prints the trace
messages of DMTCP and MANA. Any build prints them. You do not need a debug
build.

## Inspect a checkpoint image

To print the header of a checkpoint image (its memory areas, threads and
files), type:

```bash
dmtcp/util/readdmtcp.sh ckpt_rank_0/ckpt_*.dmtcp
```

## Attach gdb to a rank

The lower half loads the code of the upper half (the application and
`libmana.so`). The kernel does not load it. So `gdb` does not find its symbols by itself.
The script `dmtcp/util/gdb-dmtcp-utils.py` loads them:

```bash
gdb -p PID
(gdb) source dmtcp/util/gdb-dmtcp-utils.py
(gdb) load-symbols-library $pc
(gdb) bt
```

`load-symbols-library` also accepts a path or a part of the name of a library,
for example `libmana.so`. To see the other commands of the script, type
`dmtcp` in `gdb`. The checkpoint signal of DMTCP is `SIGUSR2`. To keep `gdb`
from stopping at each checkpoint, type `handle SIGUSR2 nostop noprint pass`.

For code in the lower half, load the symbols of the lower-half program:

```bash
(gdb) file bin/lower-half
```

## Attach gdb during a restart

To stop a restart at a stage N, from 1 to 7, set `DMTCP_RESTART_PAUSE`:

```bash
srun ... env DMTCP_RESTART_PAUSE=N bin/mana_restart &
gdb -p PID   # from another terminal, on the node of the paused rank
```

The paused rank prints its PID. The DMTCP documentation describes each stage.
For problems in the early restart, also read `src/lower-half/README` in the
MANA source tree.

To debug MANA itself from the first instruction, use `mana_launch --gdb` or
`mana_restart --gdb`. These options run the command under `gdb --args`.

## Attach gdb after a segmentation fault

Set `MANA_SEGV_DEBUG_LOOP=1` before `mana_launch`. A rank that gets a
segmentation fault then loops forever, and you can attach `gdb` to it. This is
useful when the fault does not occur under `gdb`.

## Get to the node of a rank

`gdb -p` works only on the node of the process. On a cluster, find the nodes
of your job, and connect to one of them with `ssh`:

```bash
squeue --me          # lists your jobs and their nodes
ssh NODE             # a node of your job
gdb -p PID
```

Some clusters require passwordless SSH keys between nodes, or allow `ssh` to a
compute node only through a different node. Read the documentation of your
cluster.

## Report a bug

Open an issue at <https://github.com/mpickpt/mana/issues>. Include these
items:

- The MANA version or commit, and the MPI library and its version.
- The commands that you used, and the output with `--verbose`.
- If you can, a small program that shows the problem.
