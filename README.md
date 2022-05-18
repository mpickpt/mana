# [DMTCP: Distributed MultiThreaded CheckPointing](http://dmtcp.sourceforge.net/) [![Build Status](https://travis-ci.org/dmtcp/dmtcp.png?branch=master)](https://travis-ci.org/dmtcp/dmtcp)

DMTCP is a tool to transparently checkpoint the state of multiple simultaneous
applications, including multi-threaded and distributed applications. It
operates directly on the user binary executable, without any Linux kernel
modules or other kernel modifications.

** [If you are looking for MANA (suppot for MPI), please read below.] **

Among the applications supported by DMTCP are MPI (various implementations),
OpenMP, MATLAB, Python, Perl, R, and many programming languages and shell
scripting languages. DMTCP also supports GNU screen sessions, including
vim/cscope and emacs. With the use of TightVNC, it can also checkpoint
and restart X Window applications.  The OpenGL library for 3D graphics
is supported through a special plugin.

DMTCP supports the commonly used OFED API for InfiniBand, as well as its
integration with various implementations of MPI, and resource managers
(e.g., SLURM).

To install DMTCP, see [INSTALL.md](INSTALL.md).

For an overview DMTCP, see [QUICK-START.md](QUICK-START.md).

For the license, see [COPYING](COPYING).

For more information on DMTCP, see: [http://dmtcp.sourceforge.net](http://dmtcp.sourceforge.net).

For the latest version of DMTCP (both official release and git), see:
[http://dmtcp.sourceforge.net/downloads.html](http://dmtcp.sourceforge.net/downloads.html).

---

## MANA (MPI-Agnostic, Network-Agnostic MPI)

MANA is an implementation of transparent checkpointing for MPI.  It is
built as a plugin on top of DMTCP.

We are currently concentrating on making MANA robust (especially on Cori
and Perlmutter at NERSC).  We plan to roll this out to other platforms
(CentOS 7 with MPICH, etc.), over time.  If you have the technical expertise
to help in this rollout, help is appreciated.  Please see the INSTALL
file, below, for further details.  Note that MANA currently requires
the ability to create a statically linked MPI executable (using libmpi.a).
In a later phase, this restriction will be lifted.

As MANA is in constant development, it can be handy to keep track of its
version, as well as identify what changes may have caused potential bugs. As of
Jan 2022, version tracking has been implemented - simply refer to
`include/mana-version.h` after building MANA, or use the `--version` flag with
any of the MANA commands.

** WARNING: ** MANA currently may have large runtime overhead or loss
of accuracy on restart.  This is still under development.  Please test
your application on MANA first, before using MANA.

For details of installing and using MANA, please see:
- [the MANA README file](https://github.com/mpickpt/mana/blob/master/contrib/mpi-proxy-split/README)
- [the MANA INSTALL file](https://github.com/mpickpt/mana/blob/master/contrib/mpi-proxy-split/INSTALL)
- [the MANA 'man' page](https://github.com/mpickpt/mana/blob/master/manpages/mana.1) (or 'nroff -man mana.1')
- [a tutorial for installing MANA on CentOS 7 and 8 Stream](https://github.com/mpickpt/mana/blob/master/doc/mana-centos-tutorial.txt)
