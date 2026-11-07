---
title: Checkpoint and restart internals
parent: Developer guide
nav_order: 3
description: "The DMTCP events of MANA, the Collective Clock, the P2P drain, and the restart."
---

# Checkpoint and restart internals
{: .no_toc }

1. TOC
{:toc}

---

## The goal

DMTCP can save the upper half only at a moment when the MPI library holds no
state that the upper half needs. At that moment, these conditions must be true:

- No thread is in the lower half.
- No collective call is half complete: each collective call that started on a
  rank is complete on all members of the communicator.
- No point-to-point message is in transit. Each message that a rank sent is
  in a buffer of the application or of MANA.

Before a checkpoint, MANA brings the job to such a moment. This is the
*drain*. MANA does it in the event hook of its DMTCP plugin,
`mpi_plugin_event_hook()` in `src/mpi_plugin.cpp`.

## The DMTCP events of MANA

| Event | What MANA does |
|---|---|
| `DMTCP_EVENT_INIT` | Initializes its state and its signal handlers. |
| `DMTCP_EVENT_PRESUSPEND` | Drains the collective calls, keeps threads out of the lower half, and drains the point-to-point messages. |
| `DMTCP_EVENT_PRECHECKPOINT` | Sets the checkpoint directory to `ckpt_rank_N`, and writes `header.mana` and `mpi_files.mana`. |
| `DMTCP_EVENT_RESUME` | Resets the drain counters and the Collective Clock, and lets the threads continue. |
| `DMTCP_EVENT_RESTART` | Connects to the new lower half, makes the MPI objects again, and posts the pending receives again. |
| `DMTCP_EVENT_RUNNING` | Closes the temporary file descriptors of the checkpoint and the placeholders of the restart. |
| `DMTCP_EVENT_PTHREAD_*`, `DMTCP_EVENT_THREAD_RESUME` | Records the TLS address of each upper-half thread. |

The variable `mana_state` shows the phase of a checkpoint: `RUNNING`,
`CKPT_COLLECTIVE` (the collective drain) or `CKPT_P2P` (the point-to-point
drain).

## The checkpoint sequence

When the coordinator starts a checkpoint, the checkpoint thread of each rank
does these steps in `DMTCP_EVENT_PRESUSPEND`:

1. **Wait for `MPI_Init`.** If the application is still in `MPI_Init`, wait
   until it returns. Before that, the rank has no MANA state.
2. **Drain the collective calls** with the Collective Clock
   (`drain_mpi_collective()`).
3. **Close the lower half.** Block new entries into the lower half, and wait
   until each application thread leaves it. In `blocking` mode, a thread that
   waits in `MPI_Recv` or `MPI_Send` stays there. The P2P drain releases it.
4. **Drain the point-to-point messages** (`drainP2p()`). In `blocking` mode,
   wait until the threads that the drain released leave the lower half.
5. **Complete the pending non-blocking collectives**, and the pending
   `MPI_Comm_free` barriers.

Then DMTCP suspends the application threads. In `DMTCP_EVENT_PRECHECKPOINT`,
MANA writes its files. DMTCP then writes the image of the upper half. MANA
tells DMTCP to skip the memory of the lower half
(`dmtcp_skip_memory_region_ckpting()`).

The ranks coordinate with DMTCP barriers and with the key-value database of
the DMTCP coordinator (KVDB). Each checkpoint uses its own KVDB names, which
contain the start time of the job and the number of the checkpoint
(`ckpt_kvdb_name()` in `src/seq_num.cpp`).

## Keep threads out of the lower half

A checkpoint must not stop a thread while it is in the lower half. Each wrapper
brackets its lower-half calls with `LOWER_HALF_DISABLE_CKPT()` and
`LOWER_HALF_ENABLE_CKPT()` (`src/lower_half_ckpt.h`). These macros increment
and decrement a counter in the thread. The pairs can nest.

In step 3 of the checkpoint sequence, the checkpoint thread sets
`lower_half_closed`. A thread that then calls a wrapper waits at
`LOWER_HALF_DISABLE_CKPT()`. The checkpoint thread waits until the counter of
each thread is zero. After DMTCP suspends the threads, MANA opens the lower
half again, so that the threads can enter when they resume.

## The Collective Clock

The collective drain uses the *Collective Clock* algorithm. The paper
"Enabling Practical Transparent Checkpointing for MPI: A Topological Sort
Approach" (Cluster 2024) describes it. The code is in `src/seq_num.cpp`.

### Clocks and targets

Each communicator has a clock on each of its members. The clock holds two
numbers:

- `seq`, the sequence number: the number of collective calls that this rank
  made on the communicator.
- `target`: the number that `seq` must reach before the checkpoint.

Each collective wrapper calls `commit_begin()` before the lower-half call. This
function increments `seq`. It is cheap: a lock and an increment. The wrapper
calls `commit_finish()` after the call.

### The drain

At a checkpoint, each rank uploads its `seq` for each communicator to the
KVDB, which keeps the maximum. The maximum becomes the `target` of the
communicator on all members. Then the ranks run again, with these rules:

- A rank whose clocks all reached their targets waits at its next collective
  call. It does not start that call.
- A rank with a clock below its target continues. It makes more collective
  calls until it reaches its targets.
- If a rank passes a target (because it calls a collective on a different
  communicator to reach its own targets), it raises that target. The next
  round of the drain sends the new target to the other members.

The drain runs in rounds. Each round ends with a DMTCP barrier. The drain ends
when all ranks reached their targets, no rank is in a collective call, and no
rank raised a target in the round.

### An example

Ranks 0, 1 and 2 share a communicator C. When the checkpoint starts, rank 0 is
in its 5th `MPI_Bcast` on C. Ranks 1 and 2 made only 4 collective calls on C,
and they compute.

1. The ranks upload `seq`: 5, 4 and 4. The target of C becomes 5 on all three
   ranks.
2. Rank 0 reached its target, but it is in a collective call. Ranks 1 and 2 are
   below the target, so they continue.
3. Ranks 1 and 2 call their 5th `MPI_Bcast` on C. The broadcast completes on
   all three ranks.
4. Each rank waits before its next collective call. All clocks reached their
   targets, and no rank is in a collective call. The drain ends.

### Retry

An attempt of the drain can fail. For example, a rank can compute for a long
time before it reaches its target. After 200 rounds in which no rank raised a
target, the attempt ends. The ranks then run freely for one second, and a new
attempt starts. Therefore, a late rank delays the checkpoint, but it does not
stop it.

### Non-blocking collectives

A non-blocking collective also increments `seq` when it starts. After the
drain, all members started the same collectives. MANA then completes each
pending non-blocking collective with `MPI_Test`. The request of the
application then maps to a completed request, so its later `MPI_Wait`
returns at once.

### MPI_Comm_free

The Collective Clock counts `MPI_Comm_free` too. A rank that frees a
communicator does not free the real communicator at once. It starts an
`MPI_Ibarrier` on it, and frees it when the barrier completes. Thus, no member
frees the real communicator while another member can still use it in the
drain.

## The P2P drain

After the collective drain, MANA drains the point-to-point messages. The code
is in `src/p2p_drain.cpp`. The method depends on the wait mode
(`MANA_P2P_WAIT`).

### Counters

Each rank counts the messages that it sent and received. During the drain, the
ranks add their counters in the KVDB. The drain ends when the sum of messages
received equals the sum of messages sent, and no `MPI_Isend` is pending. An
`MPI_Irecv` that no message matches stays pending. The restart posts it
again.

### Polling mode

In `polling` mode (the default), the wrapper of `MPI_Send` and `MPI_Recv`
starts an `MPI_Isend` or `MPI_Irecv` and calls `MPI_Test` in a loop. So no
thread waits in the lower half. The checkpoint thread drains in a loop, with no
barrier:

1. Complete the pending `MPI_Isend` and `MPI_Irecv` requests that can complete.
2. Test the pending non-blocking collectives.
3. Probe each communicator with `MPI_Iprobe`. Receive each message that arrived
   into a buffer of MANA.
4. Add the counters of all ranks in the KVDB.

The loop ends when the ranks received all messages. The checkpoint thread never
waits for another rank outside MPI. This is necessary: a large message
(rendezvous protocol) needs MPI calls on both ranks to make progress.

### Blocking mode

In `blocking` mode, an application thread can wait in the `MPI_Recv` of the
MPI library at the checkpoint. That receive cannot complete if no message
comes. So after the ranks received all real messages, MANA sends a *dummy message*
to each blocked receive:

1. Each rank that has a blocked `MPI_Recv` sets its bit in a bitmap in the
   KVDB.
2. Each blocked rank posts a request for its dummy to the rank that must send
   it: the source of the receive, or for `MPI_ANY_SOURCE`, a member of the
   communicator that is not blocked. The request holds the gid of the
   communicator, the tag, and the size of the receive.
3. Each rank sends the dummies posted to it.

If the lower half has `MPI_THREAD_MULTIPLE`, the checkpoint thread does these
steps inside the loop of the polling mode, with no barrier. Otherwise, it
drains first and then does the three steps with a DMTCP barrier after each
one.

The `MPI_Recv` wrapper finds that it received a dummy (`p2p_dummy_phase`). It
discards the dummy. It waits until the checkpoint ends, and then calls
`MPI_Recv` again.

### Messages in the buffer of MANA

After the checkpoint, the application can call `MPI_Recv` for a message that
the drain put in the buffer of MANA. The wrappers of `MPI_Recv`, `MPI_Irecv`,
`MPI_Probe` and `MPI_Iprobe` look in the buffer first
(`existsMatchingMsgBuffer()`, `consumeMatchingMsgBuffer()`). The buffer is in
the upper half, so it is in the checkpoint image.

### Synchronous sends

`MPI_Ssend` and `MPI_Issend` complete only when the receiver starts the
matching receive. If the drain received the message into the buffer of MANA,
the receiver did not start that receive yet. So MANA must not complete the
send at once. The receiver sends an acknowledgment when the application
receives the message from the buffer. Then the send completes. The comment
above `publishSsendNotices()` in `src/p2p_drain.cpp` describes the protocol.

## Resume

In `DMTCP_EVENT_RESUME`, MANA resets the drain counters and the Collective
Clock. It sends the acknowledgments of synchronous sends that the drain owes.
Then the application threads continue.

## Restart

`mana_restart` reads `header.mana` and sets two variables for the new lower
half: `MANA_P2P_WAIT` (the wait mode of the job) and `MANA_RESERVED_FDS` (the
file descriptors to keep free). The lower half initializes MPI and restores
the upper half (see [Architecture](architecture.html#restart)).

Then, in `DMTCP_EVENT_RESTART`, MANA does these steps:

1. Read the address of the new `lh_info`, and reset the pointers to the
   functions of the lower half.
2. Fill the table of predefined constants with the values of the new MPI
   library.
3. Make the MPI objects again: communicators, groups, datatypes and
   operations (`reconstruct_descriptors()`).
4. Reset the drain counters.
5. Post the pending `MPI_Irecv` requests again, in the order that the
   application posted them (`replayMpiP2pOnRestart()`). The MPI library matches
   receives in posting order, so the order must not change. No `MPI_Isend` is
   pending, because the drain completed all of them.
6. Open the MPI files again, from `mpi_files.mana`.

In `DMTCP_EVENT_RUNNING`, MANA closes the placeholders of the reserved file
descriptors that DMTCP did not use.
