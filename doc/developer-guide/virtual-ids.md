---
title: Virtual handles
parent: Developer guide
nav_order: 2
description: "How MANA keeps the MPI handles of the application valid across a restart."
---

# Virtual handles
{: .no_toc }

1. TOC
{:toc}

---

## Why MANA needs virtual handles

An MPI handle (for example, an `MPI_Comm`) identifies an object in the MPI
library. A restart discards the MPI library and starts a new one. The new
library gives different handles to the same objects. But the application
keeps the old handles in its memory, and the checkpoint restores them.

Therefore, MANA never gives a real handle to the application. Each wrapper
gives the application a *virtual handle*, and translates it to the real handle
at each call. At restart, MANA makes each object again in the new library, and
updates the real handle behind the virtual handle. The application sees no
change.

The code is in `src/virtual_id.{h,cpp}`.

## The handle kinds

MANA makes virtual handles for these kinds of MPI objects:

| Kind | Descriptor | What a restart does |
|---|---|---|
| `MPI_Comm` | `mana_comm_desc` | Makes the communicator again from its global ranks, and its Cartesian topology if it has one |
| `MPI_Group` | `mana_group_desc` | Makes the group again from its global ranks |
| `MPI_Datatype` | `mana_datatype_desc` | Calls the constructor of the datatype again, and commits it if the application committed it |
| `MPI_Op` | `mana_op_desc` | Calls `MPI_Op_create` again with the user function |
| `MPI_Request` | none | The P2P drain and the Collective Clock complete or replay requests (see [Checkpoint and restart internals](checkpoint-internals.html)) |
| `MPI_File` | none | Opens the file again, from `mpi_files.mana` |

MANA does not make virtual handles for `MPI_Info` and `MPI_Errhandler`.

### Predefined handles

The application gets predefined handles (`MPI_COMM_WORLD`, `MPI_INT`,
`MPI_SUM`, and so on) from `mpi.h` and from `libmpistub.so`. These values are
not virtual handles. MANA keeps a table that maps each predefined constant of
the upper half to the value of the lower half. With MPICH, both values are
usually the same. With Open MPI, a predefined handle is the address of an
object in the MPI library, so the values are different. A restart fills the
table again (`init_predefined_virt_ids()`).

`MPI_COMM_WORLD` has one more mapping. MANA makes its own duplicate of
`MPI_COMM_WORLD`, `g_world_comm`, for its internal messages. The descriptor of
`g_world_comm` is also the descriptor of `MPI_COMM_WORLD`.

## The format of a virtual handle

A virtual handle has 32 bits:

| Bits | Field | Use |
|---|---|---|
| 28 to 31 | Kind | The kind of object (communicator, group, and so on) |
| 24 to 27 | Generation | Changes each time MANA uses a slot again, so that MANA finds a stale handle. After 16 uses of a slot, the value repeats. |
| 0 to 23 | Slot | The index of the entry in the table |

The table is an array of chunks of 4096 entries. MANA never moves or frees a
chunk, so an entry keeps its address. A lookup does not take a lock, and it
costs a few instructions:

```cpp
MPI_Comm real = get_real_id((mana_mpi_handle){.comm = comm}).comm;
```

`get_real_id()` also accepts a predefined handle. It then looks up the table of
constants.

With Open MPI, a real handle has 64 bits. A virtual handle still has 32 bits,
so MANA knows that a handle with a bit set above bit 31 is not virtual.

## Descriptors

Each entry of the table holds the real handle, a pointer to a descriptor, and
a sequence number of creation. A descriptor holds what a restart needs to make
the object again:

- A **communicator** descriptor holds the size, the rank, and the global ranks
  of its members. It also holds the Cartesian topology, if there is one. It
  also holds a *gid* and a pointer to the Collective Clock of the
  communicator.
- A **group** descriptor holds the global ranks of its members.
- A **datatype** descriptor holds the constructor (contiguous, vector, struct,
  and so on), its arguments, and whether the datatype is committed.
- An **operation** descriptor holds the user function and whether it is
  commutative.

### The gid of a communicator

All members of a communicator have the same gid. Each member has its own
virtual handle for the communicator, and the values of these handles can
differ. The Collective Clock and the P2P drain use the gid to identify a
communicator in the database of the coordinator.

Each member computes the gid without communication. A communicator that a
call on a parent communicator P makes, for example `MPI_Comm_dup` or
`MPI_Comm_split`, gets a hash of these values:

- The gid of P.
- The number of earlier calls on P that made a communicator. MPI requires
  all members of P to make collective calls on P in the same order. Thus, all
  members count the same number.
- The world rank of rank 0 of the new communicator. This value makes the
  communicators of one `MPI_Comm_split` different.

Only the members of the group call `MPI_Comm_create_group`. For this call,
the number includes only the earlier calls on P with the same group and tag.
`MPI_COMM_WORLD`, `MPI_COMM_SELF` and the internal communicators of MANA get
gids from their names. The gid of `MPI_COMM_SELF` also uses the world rank.

Two communicators get the same gid only if the 64-bit hash collides. For
10,000 communicators, the probability is less than 3×10⁻¹². If two
communicators in one process get the same gid, `add_comm_clock()` stops the
process.

MANA does not use a collective call to agree on the gid. That call would be
the first collective call on the communicator, also if the application uses
the communicator much later or never. Some MPI libraries assign resources to a
communicator at its first collective call. For example, each node has a fixed
number of slots for the shared-memory collectives of Cray MPICH. A
communicator gets a slot at its first `MPI_Bcast`, `MPI_Barrier` or
`MPI_Allreduce`, if a slot is free.

### Datatypes made from freed datatypes

An application can free a datatype while a different datatype made from it
is still in use. A restart must make the freed datatype again, because the
live datatype needs it. So a datatype descriptor counts references: one from
the application, and one from each live datatype made from it. MANA releases
the descriptor when the count is zero and no pending `MPI_Isend` or
`MPI_Irecv` uses the datatype.

## Reconstruction at restart

At restart, `reconstruct_descriptors()` in `src/virtual_id.cpp` makes the
objects again in this order:

1. By kind: communicators, then groups, then datatypes, then operations.
2. In a kind, by the sequence number of creation, not by slot. A slot can be
   used again, so its order is not the order of creation.

For communicators, the order is the same on all members. This is necessary,
because the constructors are collective: all members of a communicator must
make it in the same order. Groups, datatypes and operations are local, so
their sequence can differ between ranks.

Before it makes the objects again, `reconstruct_descriptors()` calls
`MPI_Barrier` on `MPI_COMM_WORLD`. Thus, the first collective call of the new
MPI library is on `MPI_COMM_WORLD`, as in most programs after `MPI_Init`. On
Cray MPICH, `MPI_COMM_WORLD` then gets a slot for the shared-memory
collectives before the other communicators. The other communicators get slots
in the order that the application uses them after the restart. This order can
be different from the order before the checkpoint.
