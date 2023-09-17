# THIS IS AN INDEV BRANCH, WHICH CONTAINS FIXES FOR NEU DISCOVERY CKPT->RESTART.

`cd dmtcp`
`git revert d8e15bdfd57f0905a120d4b22cfe42e8d1d61264`
`git apply ../dmtcp_mtcp_restart.patch`

SEE: sbatch-sw4-bug file

cd /scratch/g.cooperman
mkdir test1

sbatch  ~/mana-dev-*/sbatch-sw4-bug
Out file:  sw4_NUMBERS.ou

## MANA (MPI-Agnostic, Network-Agnostic MPI)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of [DMTCP](https://github.com/dmtcp/dmtcp).

We are currently concentrating on making MANA robust (especially on Cori
and Perlmutter at NERSC).  We plan to roll this ot to other platforms
(CentOS 7 with MPICH, etc.), over time.  If you have the technical expertise
to help in this rollout, help is appreciated.  Please see the README.md
file, below, for further details.  Note that MANA currently requires
the ability to create a statically linked MPI executable (using `libmpi.a`).
In a later phase, this restriction will be lifted.

**Warning!** MANA currently may have large runtime overhead or loss
of accuracy on restart.  This is still under development.  Please test
your application on MANA first, before using MANA.

For details of installing and using MANA, please see:
- [MANA README file](mpi-proxy-split/README.md)
- [the MANA 'man' page](manpages/mana.1.md) (or `man ./mana.1` on a local copy)



