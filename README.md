## MANA (MPI-Agnostic, Network-Agnostic MPI)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of [DMTCP](https://github.com/dmtcp/dmtcp).

We are currently concentrating on making MANA robust (with special
testing on Perlmutter at NERSC, and now on CentOS&nbsp;7), using Slurm.
If you have the technical expertise and interest, we welcome more
collaborators.

Please see the README.md file, below, for further details.  Note that
MANA currently requires the ability to create a statically linked MPI
executable (using `libmpi.a`).  In a later phase, this restriction will
be lifted.

For details of installing and using MANA, please see:
- [MANA README file](mpi-proxy-split/README.md)
- [the MANA 'man' page](manpages/mana.1.md) (or `man ./mana.1` on a local copy)

For technical details, see:
* "Enabling Practical Transparent Checkpointing for MPI: A Topological Sort Approach",
  Yao Xu, Gene Cooperman, Cluster'24
* "Implementation-Oblivious Transparent Checkpoint-Restart for MPI",
  Yao Xu, Leonid Belyaev, Twinkle Jain,
    Derek Schafer, Anthony Skjellum, Gene Cooperman, SuperCheck-SC23 Workshop
	at SC'23.
  (production version of MANA)
* "MANA for MPI: MPI-Agnostic Network-Agnostic Transparent Checkpointing",
  Rohan Garg, Gregory Price, and Gene Cooperman, HPDC'19.
  (original academic prototype)
* [MANA internals for restart](restart_plugin/README)
