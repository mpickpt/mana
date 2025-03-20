## MANA (MPI-Agnostic, Network-Agnostic MPI)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of [DMTCP](https://github.com/dmtcp/dmtcp).

For details of installing and using MANA, please see:
- [MANA README file](mpi-proxy-split/README.md)
- [the MANA 'man' page](manpages/mana.1.md) (or `man ./mana.1` on a local copy)
- [MANA documentation](https://mana-doc.readthedocs.io/en/latest/)

For technical details, see:
* "Enabling Practical Transparent Checkpointing for MPI: A Topological
     Sort Approach",
   Yao Xu and Gene Cooperman,
   IEEE International Conference on Cluster Computing (Cluster'24)

* "Implementation-Oblivious Transparent Checkpoint-Restart for MPI",
  Yao Xu, Leonid Belyaev, Twinkle Jain,
    Derek Schafer, Anthony Skjellum, Gene Cooperman, SuperCheck-SC23 Workshop
	at SC'23.
  (near production version of MANA)
* "MANA for MPI: MPI-Agnostic Network-Agnostic Transparent Checkpointing",
  Rohan Garg, Gregory Price, and Gene Cooperman, HPDC'19.
  (original academic prototype)
