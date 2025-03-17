# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!---
# Unreleased

- Add changes here to be included in a future release
- Note the [`CHANGELOG.md standard`](https://github.com/standard/standard/blob/master/CHANGELOG.md)

TODO for all releases:

- Create a new git tag (e.g., v1.2.0)
- Update CHANGELOG.md
- Update VERSION
- Review and update any README.md files (e.g., mpi-proxy-split/README.md).
- Review and update https://github.com/mpickpt/mana-doc and propagate
  to readthedocs.io
- Finally, go to github and do a new release there.
--->

## [1.2.0] - 2025-03-19

We're pleased to announce `MANA` 1.2.0 !

<!----
- Update [`MANA`](https://github.com/mpickpt/mana) from `1.0.0` to `1.2.0`
- Update [`VERSION`](VERSION) from `1.0.0` to `1.2.0`
- Create tag v1.2.0
- NOTE: 1.1.0 was released with tag [`v1.1.01](releases/tag/v1.1.0), but VERSION was accidentally not incremented at that time.
---->

This minor version release is backward compatible with version 1.1.0 and 1.0.0.  The changelog for 1.2.0 is described relative to 1.0.0, and _not_ the intermediate 1.1.0 release.  There are many changes for numerous bug fixes, performance improvements, portabillity to many environments, and robustness across MPI applications.

Furthermore, [`detailed documentation`](https://manadocs.readthedocs.io/) of how to run MANA with or without Slurm, with examples for CentOS 7 (similar to Rocky 8) and with examples for the Perlmutter supercomputer (SUSE Enterprise) at NERSC/LBNL.  The upstream version of the documentation is at [`https://github.com/mpickpt/mana-doc`](https://github.com/mpickpt/mana-doc).

### Major changes

- MANA has been tested to support CentOS 7, Rocky 8, SUSE Enterprise Linux on Perlmutter at NERSC/LBNL, and Ubuntu 22.04i (single-node only).
- MANA has been tested to support CentOS 7, Rocky 8, SUSE Enterprise Linux on Perlmutter at NERSC/LBNL, and Ubuntu 22.04.
- MANA has been tested to support x86_64, ARM64, and RISC-V.
- MANA has been tested to support MPICH-3.x, MPICH-4.x, Open MPI-4.x, Open MPI-5.x, and [`ExaMPI`](https://github.com/LLNL/MPI-Stages/).
- Due to the underlying DMTCP, MANA requires C++14 or higher.  Note that CentOS 7 supplies an incompatible gcc-4.8 by default, and where icc is installed it must be based on a newer version of gcc than gcc-4.8.
- MANA has `not` been tested for all possible combinations of the above.
- MANA is organized as a [`DMTCP`](https://github.com/dmtcp/dmtcp) plugin.  This release has been updated to use DMTCP commit 9370f9a on the main branch.

The `mana_launch` command now directly executes MPI programs compiled with the native `mpicc` command for any of the supported MPI implementations of that site.  However, for some MPI implementations (especially MPICH-4.x), you may need to use the new flag `--use-shadowlibs`:

- `mana_launch --use-shadowlibs ... <MPI_APLICATION>`

Alternatively, you can compile an MPI application with `mpicc_mana`, to directly build an executable
that can be executed by MANA (but which cannot be executed solely by an MPI implementation without MANA.

### Changed features

#### Performance (runtime overhead)

Runtime overhead has been greatly improved, to the extent that most codes will see substantially less than 1% runtime overhead when running with MANA on top of the native MPI implementation, as opposed to directly executing solely with the native MPI implementation.  Previous to this, up to 30% runtime overhead had been observed on MPI applications that particularly stressed intensive use of MPI functions.  There were two major improvements to achieve this, concerning collective MPI operations and point-to-point MPI functions.

- Collective MPI functions:  A novel sequence number algorithm was implemented.  The sequence number algorithm is documented in: "Enabling Practical Transparent Checkpointing for MPI: A Topological Sort Approach", Cluster'24, Y. Xu and G. Cooperman
- Point-to-point functions:  A new implementation of point-to-point wrapper functions was implemented, different from the implementation in the original MANA paper ("MANA for MPI: MPI-Agnostic Network-Agnostic Transparent Checkpointing", HPDC'19, R. Garg, G. Price, G. Cooperman).  The new wrapper implementation calls MPI_Iprobe before MPI_Recv, so as to efficiently guarantee that MANA is not blocked inside an MPI function at the time of a checkpoint request..

### Modest runtime overhead on older Linux kernels (< 5.9)

On older Linux kernels, (kernel version less than 5.9), Linux did not
yet support the fsgsbase instructions for x86_64.  This primarily
affects CentOS 7 for x86_64 CPUs.

You can detect if this
affects you, when doing `./configure`.  Check if you see 'yes' or 'no' for:
> `checking if FSGSBASE for x86_64 (setting fs in user space) is available.`

If this affects you, you may see up to 5% additional runtime overhead due
to the lack of availability of these assembly instructions in user-mode.
The exact runtime overhead depends on how intensively your code calls
MPI functions.  In MANA (on x86_64 _only_), each call to an MPI function
requires resetting the pointer to the thread-local storage, which is
held in the x86_64 fs register.  Prior to Linux 5.9, this required
a kernel call and could not be done in user space.

#### MANA detailed documentation in readthedocs.io

MANA now has detailed documentation at [`readthedocs.io`](https://manadocs.readthedocs.io/).  For issues of incomplete or erroneous documentation, please notify the developers by opening an issue at the upstream repo:  
  [`https://github.com/mpickpt/mana-doc`](https://github.com/mpickpt/mana-doc).

#### Compatibility with MPI-3.x

MANA has removed support for those functions that existed in MPI-2.x, but were removed in the MPI-3.x standard.  This was necessary, since the `mpi.h` file of some MPI implementations had already removed the signatures for the MPI functions that had been removed from the standard.

## [1.1.0] - 2025-02-14 {YANKED}

This intermediate release contains only some of the important improvements described in the notes for 1.2.0.  The 1.1.0 release was not sufficiently mature, and is being yanked in favor of release 1.2.0.

This version was yanked because multiple bugs were discovered for platforms outside of Perlmutter at NERSC/LBNL.  Rather than document the differences from 1.1.x to 1.2.0, it is better to document the differences from 1.0.2 to 1.2.0.

## [1.0.2] - 2024-09-19

Bug fix and partial support for CentOS 7 (but not completely supporting it)

## [1.0.1] - 2024-09-12

Removing an obsolete configure option

## [1.0.0] - 2024-09-06

Prior to this version, development had largely concentrated on extending and adding coverage to some commonly used MPI applications.  MANA was extended as compared to the original academic prototype from the HPDC'19 paper by Garg et al.  In addition, large parts of the original prototype had to be rewritten.  The earlier version 0.9.0 represents that effort, concentrating primarily on Perlmutter at NERSC/LBNL.

This new release supported a dynamically linked lower half executable in MANA.  The original statically linked lower half was chosen in the original MANA, as supported on Cori at NERSC/LBNL because of recommendations of HPE/Cray at that time.  Furthermore, it made the design of the academic prototype easier, because one could assume that the lower-half executable had only two memory segments:  text and data.

Over time, this became a problem, because newer dependent libraries did not always have a statically linked version, and because the HPE/Cray compiler eventually evolved to create more than the original two memory segments (text and data).

By directly implementing the lower-half executable as dynamically linked, these issues are now avoided, and development was able to rapidly co-evolve with the underlying DMTCP, instead of supporting the original academic prototype.
