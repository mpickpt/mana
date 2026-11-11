---
title: Architecture
parent: Developer guide
nav_order: 1
description: "The split-process architecture of MANA: the upper half, the lower half, and the boundary."
---

# Architecture
{: .no_toc }

1. TOC
{:toc}

---

## The problem

A transparent checkpointing package for MPI has two problems that other
programs do not have:

1. **The state of the MPI library.** The MPI library holds network
   connections, queued messages, and memory that the network hardware uses. A
   checkpoint cannot save this state, and a restart on new nodes cannot use it.
2. **The variety of MPI libraries and networks.** A package that saves the
   internal state of one MPI library must change for each MPI library and each
   network. Earlier packages for MPI did this, and each one supported only a
   small set of libraries.

MANA solves both problems with *split processes*.

## Split processes

MANA divides each MPI process into two programs in one address space:

- The **upper half** holds the application, DMTCP, the MANA plugin
  (`libmana.so`), and the MPI wrappers of MANA. A checkpoint saves the upper
  half.
- The **lower half** holds the real MPI library, the libraries that it uses
  (UCX, libfabric, and the network drivers), and a C library of its own. A
  checkpoint does not save the lower half. A restart discards it and starts a
  new one.

The upper half calls the lower half only through the MPI interface, and for
its memory mappings (see [Memory](#memory)).
Therefore, the upper half holds no state that is specific to one MPI library
or to one network. A restart starts a new lower half, which initializes MPI
again. Then MANA makes the MPI objects of the application again, in the new
MPI library.

```text
 +--------------------------------------------+
 | Upper half (saved in the checkpoint)       |
 |   application, libc, DMTCP, libmana.so,    |
 |   libmpistub.so (in place of libmpi.so)    |
 +---------------------+----------------------+
                       | MPI calls: virtual handles are translated,
                       | thread-local storage is switched
 +---------------------v----------------------+
 | Lower half (discarded at checkpoint)       |
 |   lower-half program, libmpi.so, UCX or    |
 |   libfabric, its own libc                  |
 +--------------------------------------------+
```

The design has one cost. A checkpoint must restart with the same MPI library,
because the upper half holds data that came from that library (for example,
the values of MPI constants). But the same MANA source works with each MPI
library.

## Launch

`mana_launch` runs this command for each rank:

```text
dmtcp_launch --kernel-loader ... --with-plugin libmana.so  lower-half  PROGRAM ARGS
```

The lower-half program, `bin/lower-half`, does these steps:

1. It turns off address space randomization with
   `personality(ADDR_NO_RANDOMIZE)`, and executes itself again. Then the
   memory layout is the same at launch and at restart.
2. It calls `MPI_Init_thread`. In the `blocking` wait mode, it asks for
   `MPI_THREAD_MULTIPLE`. Otherwise, it asks for `MPI_THREAD_SINGLE`.
3. It fills `lh_info`, a structure that tells the upper half about the lower
   half: the address of its thread-local storage, its memory functions, a
   table of its MPI functions, and the values of its MPI constants. It puts
   the address of `lh_info` in the variable `MANA_LH_INFO_ADDR`.
4. It acts as a kernel loader. It loads the dynamic loader (`ld.so`) of
   *PROGRAM*, makes a new stack with the arguments and environment, and jumps
   to the entry point of `ld.so`.

Then the `ld.so` of the upper half loads the application, DMTCP and
`libmana.so`, as in a normal process. The application calls `MPI_Init`, and
the wrapper of `MPI_Init` sets up MANA. It does not initialize MPI again,
because the lower half did that.

## The stub MPI library

The application needs an MPI library to load, but the real MPI library must
stay out of the upper half. The constructors of the MPI library and of its
network libraries would otherwise run in the upper half. For example, the
constructor of UCX makes SysV shared memory, which DMTCP cannot restore.

So the upper half loads a stub library, `libmpistub.so`, in place of the MPI
library. The lower half sets `LD_AUDIT` to `libmpistub_audit.so`. This audit
module of `ld.so` replaces each MPI library that the application needs, or
loads with `dlopen()`, with `libmpistub.so`. The audit module works for a
program with an `RPATH` too. Therefore, a program that you built with the
normal MPI compiler runs under MANA as it is, if it uses the MPI library as a
shared library (the default).

The stub defines the MPI functions and variables that `mpi.h` declares. The
MPI functions of `libmana.so` come first in the search order, so the
application calls them. With Open MPI, a predefined handle such as
`MPI_COMM_WORLD` is the address of an object in `libmpi.so`. So the stub also
defines these objects.

## The boundary between the halves

Each MPI call crosses from the upper half to the lower half. An MPI wrapper in
`libmana.so` does these steps:

1. It blocks checkpoints, so that no checkpoint starts while a thread is in
   the lower half.
2. It translates the virtual MPI handles of the application to the real
   handles of the lower half (see [Virtual handles](virtual-ids.html)).
3. It switches the thread-local storage to the lower half
   (`JUMP_TO_LOWER_HALF`).
4. It calls the real MPI function through the function table of the lower
   half (`NEXT_FUNC`).
5. It switches the thread-local storage back (`RETURN_TO_UPPER_HALF`).
6. It makes virtual handles for the new MPI objects, and lets checkpoints
   start again.

[MPI wrappers](wrappers.html) shows the code.

### Thread-local storage

Each half has its own C library, and so its own thread-local storage (TLS).
On x86_64, the FS register holds the address of the TLS. Before a call into
the lower half, MANA sets FS to the TLS of the lower half. After the call, MANA
sets FS back. The code is in `src/lower-half/switch-context.{h,cpp}`.

MANA uses the `wrfsbase` and `rdfsbase` instructions when the kernel allows
them (Linux 5.9 or later, on a processor with `FSGSBASE`). Otherwise, MANA uses
the system call `arch_prctl`, which is slower.

{: .warning }
MANA compiles its code with `-fno-stack-protector`. The stack protector of
GCC reads a value from the TLS at the start and the end of a function. In a
function that switches FS, the two values come from different TLS blocks, and
the check fails.

### Memory

Both halves use one address space, so they must not map memory over each
other. The lower half controls the memory of the upper half:

- The `mmap()` and `munmap()` of the upper half go to the lower half
  (`src/uh_wrappers.cpp`). The lower half puts upper-half mappings in an
  arena, and never maps over an existing mapping.
- The kernel's `brk` belongs to the lower half. The `sbrk()` of the upper-half
  C library moves a break of its own, in a region that the lower half
  reserves.
- The lower half also handles `mremap()` of the upper half, so that `realloc()`
  of a large block stays in the arena.

At a checkpoint, DMTCP asks MANA which memory areas to save
(`dmtcp_skip_memory_region_ckpting()` in `src/mpi_plugin.cpp`). MANA skips the
areas of the lower half.

### Exit

When the application calls `exit()`, MANA also runs the exit handlers of the
lower half. The MPI library needs them. For example, MPICH tells the process
manager in an exit handler that the rank finished. Without it, the process
manager can kill the other ranks.

## Restart

`mana_restart` runs `bin/lower-half --restore` for each rank, with the options
of DMTCP. The lower half does these steps:

1. It initializes MPI in the wait mode of the checkpoint, from `header.mana`.
2. It gets its MPI rank N, and selects the image in `ckpt_rank_N`.
3. It writes the address of its new `lh_info` to a file in `/tmp`. The
   environment of the upper half comes from the checkpoint, so a variable
   cannot carry the address.
4. It restores the memory of the upper half from the image, with the restart
   code of DMTCP, and jumps into the upper half.

DMTCP then restores the threads and files of the upper half, and MANA makes
the MPI objects of the application again. See
[Checkpoint and restart internals](checkpoint-internals.html#restart).

The order solves a problem. A rank must know its MPI rank to choose its image,
and it must initialize MPI to know its rank. The lower half initializes MPI
before it restores the upper half, so it knows its rank in time.

### File descriptors at restart

DMTCP restores each file descriptor of the upper half to its old number. If
the new MPI library already uses that number (for example, for a socket), the
restore closes the socket. To prevent this, `header.mana` lists the file
descriptors that were open at the checkpoint. Before it initializes MPI, the
lower half puts a placeholder on each of these numbers. After the restore,
MANA closes the placeholders that DMTCP did not use.
