---
title: Home
layout: home
nav_order: 1
description: "MANA checkpoints and restarts MPI jobs. It is a plugin of DMTCP."
permalink: /
---

# MANA
{: .fs-9 }

MPI-Agnostic, Network-Agnostic checkpointing for MPI jobs.
{: .fs-6 .fw-300 }

[Quick start](quick-start.html){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[MANA on GitHub](https://github.com/mpickpt/mana){: .btn .fs-5 .mb-4 .mb-md-0 }

---

## About MANA

MANA checkpoints and restarts MPI jobs. A checkpoint writes the state of each
MPI process to disk while the job runs. A restart starts the job again from
that state. For example, a job can continue after it reaches the time limit of
its allocation, or after a node fails. The MPI program needs no change and no
relinking.

MANA is a plugin of [DMTCP](https://dmtcp.github.io/), a package for
transparent checkpointing. DMTCP saves the memory, threads and open files of
each process. MANA adds the part that is specific to MPI. Before a checkpoint,
MANA brings the MPI library to a state where no message is in transit. After a
restart, MANA makes the MPI objects of the application again.

## How MANA works

MANA divides each MPI process into two parts in one address space. The *upper
half* holds the application, DMTCP and MANA. The *lower half* holds the MPI
library and the network libraries that it uses. A checkpoint saves only the
upper half. A restart starts a new lower half, initializes MPI again, and
restores the upper half into the same process.

Therefore, MANA never saves the internal state of the MPI library or of
the network. This is the meaning of the name. MANA uses only the MPI
interface of the library, so the same design works with different MPI
libraries and different networks. However, a job must restart with the
same MPI library as at the checkpoint.
[Architecture](developer-guide/architecture.html) explains the design.

## Supported software

| Item | Supported |
|---|---|
| MPI libraries | MPICH and libraries based on MPICH (for example, Cray MPICH), Open MPI 4.1 and 5.0 |
| Languages | C, C++ and Fortran |
| Operating system | Linux on x86_64 |
| Job launchers | `mpirun`, and `srun` of Slurm |

The continuous integration of MANA tests it on Ubuntu 26.04 with MPICH and with
Open MPI. [MPI support and limitations](user-guide/mpi-support.html) lists the
MPI features that MANA supports.

## The current release

The current release is **MANA 1.6.0**. It adds support for Open MPI, moves the
sources to a new directory layout, and fixes hangs at checkpoint and restart.
See the [release notes](https://github.com/mpickpt/mana/releases).

## Where to go next

- [Quick start](quick-start.html): build MANA and checkpoint an example program.
- [Install](install.html): the requirements and build options.
- [User guide](user-guide/): the commands, clusters with Slurm, environment
  variables, limitations and debugging.
- [Developer guide](developer-guide/): the internals of MANA, and how to add
  an MPI wrapper or a test.
- [Publications](publications.html): the papers about MANA, and how to cite
  MANA.

To report a bug or ask a question, open an issue at
<https://github.com/mpickpt/mana/issues>.
