## MANA (MPI-Agnostic, Network-Agnostic MPI)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of [DMTCP](https://github.com/dmtcp/dmtcp).

For details of installing and using MANA, please see:
- [MANA documentation (https://mana-doc.readthedocs.io/en/latest/) ](https://mana-doc.readthedocs.io/en/latest/)
- [the MANA 'man' page](manpages/mana.1.md) (or `man ./mana.1` on a local copy)

As seen in the MANA documentation, for install and a quick start, do:

    git clone https://github.com/mpickpt/mana
    cd mana
    git submodule init
    git submodule update
    ./configure
    make -j8
    # Testing on a single host (but see MANA docs for running on a cluster):
    PATH_TO_MANA/bin/mana_coordinator
    PATH_TO_MANA/bin/mana_launch [mana options] [user_program and args]

---

To cite this project in a publicatoin, please cite:

* "MANA for MPI: MPI-Agnostic Network-Agnostic Transparent Checkpointing",
  Rohan Garg, Gregory Price, and Gene Cooperman, HPDC'19.
  (original academic prototype)

For other MANA publications with updates to this project, see:
* "Enabling Practical Transparent Checkpointing for MPI: A Topological
     Sort Approach",
   Yao Xu and Gene Cooperman,
   IEEE International Conference on Cluster Computing (Cluster'24)

* "Implementation-Oblivious Transparent Checkpoint-Restart for MPI",
  Yao Xu, Leonid Belyaev, Twinkle Jain,
    Derek Schafer, Anthony Skjellum, Gene Cooperman, SuperCheck-SC23 Workshop
	at SC'23.
  (near production version of MANA)
