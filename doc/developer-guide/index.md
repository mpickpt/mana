---
title: Developer guide
nav_order: 5
has_children: true
permalink: /developer-guide/
description: "The internals of MANA, for developers who change MANA or add MPI wrappers."
---

# Developer guide
{: .no_toc }

This guide describes the internals of MANA. It is for developers who change
MANA, add an MPI wrapper, or debug MANA itself. It assumes that you know MPI,
and that you know how a DMTCP plugin works. For DMTCP plugins, see the
[DMTCP documentation](https://dmtcp.github.io/plugins.html).
{: .fs-5 .fw-300 }

1. TOC
{:toc}

---

## Pages

| Page | Contents |
|---|---|
| [Architecture](architecture.html) | The split process: the upper half, the lower half, and the boundary between them |
| [Virtual handles](virtual-ids.html) | How MANA keeps the MPI handles of the application valid across a restart |
| [Checkpoint and restart internals](checkpoint-internals.html) | The DMTCP events of MANA, the Collective Clock, the P2P drain, and the restart |
| [MPI wrappers](wrappers.html) | How a wrapper works, the generated files, and how to add a wrapper |
| [Testing](testing.html) | The end-to-end tests, the benchmarks, and continuous integration |

## The source tree

| Directory or file | Contents |
|---|---|
| `src/` | The MANA plugin, `libmana.so`. |
| `src/mpi_plugin.cpp` | The DMTCP event hook of MANA: checkpoint, resume and restart. |
| `src/virtual_id.{h,cpp}` | The virtual handles and their descriptors. |
| `src/seq_num.{h,cpp}` | The Collective Clock, which drains the collective calls before a checkpoint. |
| `src/p2p_drain.{h,cpp}` | The drain of point-to-point messages, and the replay of pending receives at restart. |
| `src/lower_half_ckpt.{h,cpp}` | Keeps checkpoints out of the lower half. |
| `src/uh_wrappers.{h,cpp}` | Sends `mmap()` and `munmap()` of the upper half to the lower half. Finds the lower half at launch and restart. |
| `src/mpi-wrappers/` | The MPI wrappers, the stub MPI library `libmpistub.so`, and the audit module `libmpistub_audit.so`. |
| `src/lower-half/` | The lower-half program, `lower-half`. |
| `scripts/` | The commands `mana_coordinator`, `mana_launch`, `mana_status` and `mana_restart`. |
| `test/` | The end-to-end tests and their driver, `autotest.py`. |
| `examples/` | The example program `ring`. |
| `bench/` | The benchmarks `virtid_bench` and `synthetic_app`. |
| `manpages/` | The man page `mana.1`, made from `mana.1.md`. |
| `doc/` | This website, built with Jekyll and the Just the Docs theme. |
| `dmtcp/` | DMTCP, as a git submodule. |
| `util/hooks/` | The git hook that checks the style of new commits. |

`make` puts the built files into `bin/` (commands and `lower-half`) and
`lib/dmtcp/` (libraries). Do not edit files in these directories.

## Rules for contributions

- **License header.** Each C and C++ source file starts with the LGPL header.
  Copy it from `src/mpi_plugin.cpp`.
- **Style.** `make` links the git hook `util/hooks/pre-commit`. The hook runs
  the style checker of DMTCP on the changed C and C++ files, and rejects
  whitespace errors. Format new code like the code around it.
- **Generated files.** Do not edit the generated wrapper files. Edit the
  generator or its input file. See [MPI wrappers](wrappers.html).
- **Build files.** Each directory has a `Makefile.in`. `./configure` makes the
  `Makefile` from it. Edit the `Makefile.in`, not the `Makefile`.
- **Documentation.** When a change makes a page of this website wrong,
  update the page in `doc/` in the same pull request. To see the pages on
  your computer, run `bundle install` and `bundle exec jekyll serve` in
  `doc/`, with Ruby 3.3. Then open `http://127.0.0.1:4000/mana/`.
- **Tests.** Run `make check` before you open a pull request. Add a test for
  new behavior. See [Testing](testing.html).
- **Pull requests.** GitHub Actions runs the tests on each pull request to
  `main`, with MPICH and with Open MPI.
