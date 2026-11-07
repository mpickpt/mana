---
title: User guide
nav_order: 4
has_children: true
permalink: /user-guide/
description: "How to checkpoint and restart MPI jobs with MANA."
---

# User guide

This guide tells how to checkpoint and restart MPI jobs with MANA. It assumes
that you built MANA (see [Install](../install.html)) and that you did the
[Quick start](../quick-start.html).

MANA builds on DMTCP. The MANA commands are wrappers of the DMTCP commands,
and they accept the DMTCP options too. This guide covers only the topics that
are specific to MANA. For the other topics, see the
[DMTCP documentation](https://dmtcp.github.io/).

| Page | Contents |
|---|---|
| [Checkpoint and restart](checkpoint-restart.html) | The ways to checkpoint, the checkpoint files, and restart |
| [Commands](commands.html) | The options of `mana_coordinator`, `mana_launch`, `mana_status` and `mana_restart` |
| [Clusters and Slurm](clusters.html) | Jobs on more than one node, `srun`, and notes for some clusters |
| [Environment variables](environment.html) | The variables that change how MANA runs |
| [MPI support and limitations](mpi-support.html) | The MPI features that MANA supports, and its limitations |
| [Debugging](debugging.html) | What to do when a checkpoint or a restart fails |
