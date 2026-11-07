---
title: Clusters and Slurm
parent: User guide
nav_order: 3
description: "Run MANA jobs on clusters with Slurm, on one node or on many nodes."
---

# Clusters and Slurm
{: .no_toc }

1. TOC
{:toc}

---

## The steps on a cluster

Most clusters use the Slurm job scheduler. A MANA job on a cluster uses the
same commands as on one host:

1. Get an allocation of compute nodes, with `salloc` (interactive) or `sbatch`
   (batch).
2. On a node of the allocation, start a coordinator with `mana_coordinator`.
3. Launch the MPI job with `srun mana_launch PROGRAM`.
4. Restart the job in the same way, with `srun mana_restart`.

For example:

```bash
salloc -N 2 -t 01:00:00          # or the command of your cluster
bin/mana_coordinator -i 600      # checkpoint every 10 minutes
srun -N 2 bin/mana_launch ./my_app
```

To restart the job, in the same allocation or in a new one:

```bash
bin/mana_coordinator -i 600
srun -N 2 bin/mana_restart
```

## A batch job that checkpoints and restarts

A batch job can restart from its last checkpoint if a checkpoint exists. Thus,
you can submit the same script again after the job reaches its time limit:

```bash
#!/bin/bash
#SBATCH -N 2
#SBATCH -t 02:00:00

MANA=$HOME/mana                     # the MANA directory
$MANA/bin/mana_coordinator -i 1800  # checkpoint every 30 minutes
if [ -d ckpt_rank_0 ]; then
  srun $MANA/bin/mana_restart
else
  srun $MANA/bin/mana_launch ./my_app
fi
```

Each new checkpoint replaces the previous one. If the time limit stops the job
while it writes a checkpoint, `mana_restart` refuses that checkpoint, because
it is incomplete. Thus, choose an interval that is much shorter than the time
limit. If `mana_restart` refuses a checkpoint, move the `ckpt_rank_*`
directories to a different location. The next submission then starts the job
from the beginning.

## srun or mpirun

On most clusters with Slurm, `srun` launches MPI jobs. Some clusters prefer
`mpirun`, even in a Slurm allocation, and Open MPI usually prefers `mpirun`.
Use the launcher that your cluster recommends for the MPI library. MANA works
with both.

## Jobs on more than one node

All ranks must find the coordinator. The coordinator runs on the node where
you start `mana_coordinator`, and the ranks connect to it over TCP. Each rank
reads the host and port of the coordinator from the coordinator file in the
home directory (see
[The coordinator file](commands.html#the-coordinator-file)).

Therefore, the home directory must be on a file system that all nodes share.
On most clusters, it is. `mana_launch` and `mana_restart` stop with an error if
they cannot read the coordinator file.

The compute nodes must be able to connect to the coordinator. If `salloc`
leaves your shell on a login node that the compute nodes cannot reach, start
`mana_coordinator` on a node of the allocation.

The checkpoint directory must also be on a file system that all nodes share.
At restart, rank N can run on a different node, and it reads `ckpt_rank_N`
there.

## The checkpoint thread

DMTCP adds a checkpoint thread to each process. This thread sleeps until a
checkpoint starts. Thus, each rank under MANA has one more thread than the
same rank without MANA. This is not important, except when the job puts many
ranks on each core.

{: .tip }
When ranks share a core, set `MANA_P2P_WAIT=blocking`. The default mode,
`polling`, is much slower when ranks share a core. See
[Environment variables](environment.html).

## Notes for some clusters

The details of Slurm are different on each cluster. These notes give the
details for two clusters. Partition names, modules and limits change over
time. Check the documentation of your cluster for the current values.

### Perlmutter (NERSC)

Perlmutter uses SUSE Linux Enterprise and Cray MPICH. See the
[NERSC documentation](https://docs.nersc.gov/getting-started/).

- Build MANA on a login node. With the Cray Programming Environment,
  `./configure` uses `cc`, `CC` and `ftn`. No other settings are necessary.
- For an interactive allocation on CPU nodes, use `salloc` with
  `--constraint cpu`:

  ```bash
  salloc --qos interactive --nodes 1 --time 04:00:00 \
         --constraint cpu --account YOUR_ACCOUNT
  ```

- Launch with `srun`. The home directory is shared by all nodes.
- With Cray MPICH, `MANA_P2P_WAIT=blocking` can be faster than the default
  mode.

### Explorer (Northeastern University)

Explorer uses Rocky Linux 9. See the
[Research Computing documentation](https://rc.northeastern.edu).

- Build MANA on a compute node. The login nodes do not allow long commands.
- Load a compiler and an MPI library with `module`. For example:

  ```bash
  module avail gcc mpich openmpi
  module load mpich
  ```

- For an interactive allocation, use `srun --pty`. Add `--constraint=ib` to
  get nodes with InfiniBand. Without it, you can get a node with only
  Ethernet.

  ```bash
  srun --partition=short --nodes=1 --ntasks=8 --cpus-per-task=1 \
       --time=08:00:00 --mem=8GB --constraint=ib --pty /bin/bash
  ```

- Launch with `mpirun -np N`.

### Other clusters

If your cluster needs other steps, please tell us. Open an issue at
<https://github.com/mpickpt/mana/issues>.
