---
title: MPI support and limitations
parent: User guide
nav_order: 5
description: "The MPI features that MANA supports, and the known limitations of MANA."
---

# MPI support and limitations
{: .no_toc }

1. TOC
{:toc}

---

## How MANA supports an MPI function

MANA has a wrapper for each MPI function that it supports. The wrapper calls
the function of the MPI library, and records the data that a restart needs.
For an MPI function that MANA does not support, MANA has a wrapper that stops
the program with this message:

```text
wrapper: `MPI_Win_create` not implemented
```

The list of these functions is in
`src/mpi-wrappers/mpi_unimplemented_wrappers.txt` in the MANA source tree. A
program that calls one of them stops at the first call. It does not give
incorrect results.

## Supported MPI features

| Area | Supported |
|---|---|
| Initialization | `MPI_Init`, `MPI_Init_thread` up to `MPI_THREAD_FUNNELED`, `MPI_Finalize`, `MPI_Abort` |
| Point-to-point | `MPI_Send`, `MPI_Ssend`, `MPI_Rsend`, `MPI_Recv`, `MPI_Isend`, `MPI_Issend`, `MPI_Irecv`, `MPI_Sendrecv`, `MPI_Sendrecv_replace`, `MPI_Probe`, `MPI_Iprobe`, `MPI_ANY_SOURCE`, `MPI_ANY_TAG`, `MPI_PROC_NULL` |
| Completion | `MPI_Wait`, `MPI_Waitall`, `MPI_Waitany`, `MPI_Test`, `MPI_Testall`, `MPI_Testany`, `MPI_Request_get_status` |
| Blocking collectives | `MPI_Barrier`, `MPI_Bcast`, `MPI_Reduce`, `MPI_Allreduce`, `MPI_Reduce_scatter`, `MPI_Scan`, `MPI_Gather(v)`, `MPI_Scatter(v)`, `MPI_Allgather(v)`, `MPI_Alltoall(v)` |
| Non-blocking collectives | `MPI_Ibarrier`, `MPI_Ibcast`, `MPI_Ireduce`, `MPI_Iallreduce`, `MPI_Ireduce_scatter`, `MPI_Iscan`, `MPI_Igather(v)`, `MPI_Iscatter(v)`, `MPI_Iallgather(v)`, `MPI_Ialltoall(v)` |
| Communicators | `MPI_Comm_dup`, `MPI_Comm_split`, `MPI_Comm_split_type`, `MPI_Comm_create`, `MPI_Comm_create_group`, `MPI_Comm_free`, `MPI_Comm_compare`, `MPI_COMM_SELF` |
| Groups | `MPI_Comm_group`, `MPI_Group_incl`, `MPI_Group_translate_ranks`, and the queries of size and rank |
| Topologies | Cartesian topologies (`MPI_Cart_create`, `MPI_Cart_sub`, `MPI_Cart_shift`, `MPI_Cart_map` and the queries) |
| Attributes | `MPI_Comm_create_keyval`, `MPI_Comm_set_attr`, `MPI_Comm_get_attr`, `MPI_Comm_delete_attr`, and the MPI-1 calls `MPI_Keyval_create` and `MPI_Attr_*` |
| Datatypes | Contiguous, vector, hvector, indexed, hindexed, struct, dup and resized datatypes, `MPI_Type_commit`, `MPI_Type_free`, `MPI_Pack` |
| Reductions | User-defined operations (`MPI_Op_create`, `MPI_Op_free`) |
| Memory | `MPI_Alloc_mem`, `MPI_Free_mem` |
| Tools | The `MPI_T` interface |
| Languages | C, C++, and Fortran with `mpif.h` |

A program can also load the MPI library with `dlopen()`.

## Unsupported MPI features

MANA does not support these features:

- `MPI_THREAD_SERIALIZED` and `MPI_THREAD_MULTIPLE`. `MPI_Init_thread`
  supplies `MPI_THREAD_FUNNELED` instead, and rank 0 prints a warning.
- One-sided communication (`MPI_Win_*`, `MPI_Put`, `MPI_Get`,
  `MPI_Accumulate`).
- Dynamic processes (`MPI_Comm_spawn`, `MPI_Comm_connect`, and the related
  calls) and intercommunicators.
- Persistent requests (`MPI_Send_init`, `MPI_Recv_init`, `MPI_Start`),
  buffered sends (`MPI_Bsend`), `MPI_Cancel` and `MPI_Request_free`.
- Matched probes (`MPI_Mprobe`, `MPI_Improbe`, `MPI_Mrecv`, `MPI_Imrecv`).
- `MPI_Waitsome` and `MPI_Testsome`.
- `MPI_Exscan`, `MPI_Alltoallw`, `MPI_Reduce_scatter_block`, their
  non-blocking forms, and the neighborhood collectives.
- Graph topologies and distributed graph topologies.
- Some group calls: `MPI_Group_excl`, `MPI_Group_union`,
  `MPI_Group_intersection`, `MPI_Group_difference`, and the range calls.
- `MPI_Info` objects, and names of communicators (`MPI_Comm_set_name`).
- Some datatype calls: `MPI_Type_create_subarray`, `MPI_Type_create_darray`,
  `MPI_Type_create_indexed_block`, `MPI_Type_get_envelope`,
  `MPI_Type_get_contents`, and `MPI_Unpack`.
- The Fortran `mpi_f08` module.
- MPI-IO. MANA has wrappers for the main `MPI_File` calls, but no test covers
  them, and a restart does not restore all of the file state.
- The MPI-2 functions that MPI-3 removed. Some MPI libraries no longer declare
  them in `mpi.h`.

If your program needs one of these features, please open an issue at
<https://github.com/mpickpt/mana/issues>.

## Limitations of checkpoint and restart

- **Restart with the same job configuration.** Use the same number of ranks
  and the same MPI library as the original job.
- **No checkpoint after `MPI_Finalize`.** After a rank calls `MPI_Finalize`,
  MANA does not checkpoint the job.
- **A checkpoint can wait for a late rank.** Before a checkpoint, each
  collective call that started must complete on all ranks of its
  communicator. If one rank is late to a collective, the checkpoint waits
  until that rank arrives. For example, a rank that computes for 10 seconds
  before an `MPI_Barrier` can delay the checkpoint by up to 10 seconds.
- **A checkpoint during `MPI_Init` waits.** The checkpoint starts after all
  ranks complete `MPI_Init`.
- **Polling when ranks share a core.** The default wait mode, `polling`, is
  much slower when two ranks share a core. Use `MANA_P2P_WAIT=blocking` in this
  case.
- **The libfabric `sockets` provider.** With the `ch4:ofi` device of MPICH and
  the `sockets` provider of libfabric, a restart can hang. To prevent this,
  select the `tcp` provider with `FI_PROVIDER=tcp`.
- **Containers.** MANA must turn off address space randomization. See
  [Containers](../install.html#containers).
- **Processors.** MANA 1.6.0 runs only on x86_64.
- **GPUs.** MANA does not save the memory of GPUs. DMTCP has an optional CUDA
  plugin. See the [DMTCP documentation](https://dmtcp.github.io/).

## Performance

MANA adds a small cost to each MPI call. The upper half translates the MPI
handles of the call, and it switches the thread-local storage to the lower
half and back. Most applications see a small overhead. A program that makes
many small MPI calls sees more.

On Linux kernels older than 5.9, or on processors without the `FSGSBASE`
instructions, each switch needs a system call. In this case, the overhead can
be a few percent higher.

The benchmarks in `bench/` measure the overhead of MANA on your system. See
[Testing](../developer-guide/testing.html).
