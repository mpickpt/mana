**Contents:**
- [General notes for building, testing and running on Cori supercomputer at NERSC:](#general-notes-for-building-testing-and-running-on-cori-supercomputer-at-nersc)
  - [1. Setting up MANA from its Github repository](#1-setting-up-mana-from-its-github-repository)
  - [2. Building MANA](#2-building-mana)
  - [3. Testing MANA](#3-testing-mana)
    - [3a. Launching an MPI application](#3a-launching-an-mpi-application)
    - [3b. Checkpointing an MPI application](#3b-checkpointing-an-mpi-application)
    - [3c. Restarting an MPI application](#3c-restarting-an-mpi-application)
- [Debugging internals of MANA:](#debugging-internals-of-mana)
- [Building and testing on ordinary CentOS/Ubuntu (not the Cori supercomputer):](#building-and-testing-on-ordinary-centosubuntu-not-the-cori-supercomputer)
  - [Building outside of Cori for MPICH:](#building-outside-of-cori-for-mpich)
- [Testing and Running without Slurm](#testing-and-running-without-slurm)

See [doc/mana-centos-tutorial.txt](../doc/mana-centos-tutorial.txt) for
detailed, step-by-step instructions for installing MANA on CentOS 7 and CentOS
Stream.

---

# General notes for building, testing and running on Cori supercomputer at NERSC:

The main pointers for background information on MANA are:

* MANA at github:
  https://github.com/mpickpt/mana

* MANA documentation:
  https://docs.google.com/document/d/1pT25gvMNeT1Vz4SU6Gp4Hx9MGLfkK8ZK-sSOwtu5R50/edit

The main scripts for this:
  * `mana_coordinator`
  * `mana_launch`
  * `mana_restart`
  * `mana_status`

See `man mana` or `nroff -man MANA_ROOT_DIR/manpages/mana.1` for the MANA man page.

## 1. Setting up MANA from its Github repository

   In this tutorial, we'll install MANA in the `$HOME` directory, but really
   it's up to your personal preference where you want to install it.

   ```bash
   $ git clone https://github.com/mpickpt/mana.git
   $ cd mana
   $ export MANA_ROOT=$PWD
   ```

## 2. Building MANA

   The following Cori-specific modules are currently incompatible with MANA.

   ```bash
   $ module unload altd darshan craype-hugepages2M
   $ git submodule update --init
   $ ./configure
   $ make -j mana
   ```

## 3. Testing MANA

   For convenience, add the MANA bin directory to your PATH.
 
   ```bash
   $ export PATH=$PATH:$MANA_ROOT/bin
   ```
   
   Optionally, you can make this path persistent by doing:

   ```bash
   $ echo 'export PATH=$PATH:$MANA_ROOT/bin' >> ~/.bashrc
   ```
 
   On Cori, we need to allocate resources to run a job. The command above can be
modified according to your needs.

   ```bash
   $ salloc -N 1 -C haswell -q interactive -t 01:00:00
   ```

There are many MPI test programs to use for testing MANA in the directory
$MANA_ROOT/mpi-proxy-split/test. MANA currently uses files of type `.mana.exe`.

This restriction will be removed in the future, and MANA will handle MPI
applications compiled in your favorite way. Currently, to create a `.mana.exe`
file, go to the test directory and delete the `ping_pong.mana.exe` file and do
`make ping_pong.mana.exe`. Then follow that recipe for compiling your own code
for MANA.

### 3a. Launching an MPI application

The MANA directory comes with many test MPI applications that can be found in
mpi-proxy-plugin/test. Depending on the application, you may require more than
one MPI process running -- for example, `ping_pong.mana.exe` requires two. To
support this, change the argument after -np accordingly. For this tutorial,
we'll use `mpi_hello_world.mana.exe`, which can run with one MPI process.

  ```bash
  $ mana_coordinator
  $ srun -n 1 mana_launch mpi-proxy-split/test/mpi_hello_world.mana.exe
  ```

If the application is launched properly, you should see the following printed:

  ```bash
  Hello world from processor test, rank 0 out of 1 processors
  Will now sleep for 500 seconds ...
  ```

From here, we can either exit the program (through `CTRL-C`), or continue on to
the next set of instructions to checkpoint and restart the application.

### 3b. Checkpointing an MPI application

Here, we want to either open another terminal, or launch the MPI process in the
background. The following demonstrates the latter.

If an application is already launched, we do `CTRL-Z`, followed by:

  ```bash
  $ bg
  ```

Otherwise, we'll launch an application the same way as above, but as a
background process (note the &).

  ```bash
  $ mana_coordinator
  $ mpirun -np 1 mana_launch \
      contrib/mpi-proxy-split/test/mpi_hello_world.mana.exe &
  ```

Subsequently, we only need the following command to checkpoint the application.

  ```bash
  $ mana_status --checkpoint
  ```

This creates one, or multiple, folder(s) in the current directory depending on
the number of copies of the process running, `ckpt_rank_{RANK OF PROCESS}`, each
containing checkpoint images. It is important to note that mana_launch will
refuse to execute if these files are present, since otherwise, MANA would
overwrite previous checkpoint files.

This is an example of manual checkpointing, and there are other ways of
checkpointing applications. The second is to use interval checkpointing with
the `-i` flag of `mana_launch`; more information can be found on the MANA manpage.

To see if a coordinator (or other process) is already running, do:
  ```bash
  ps uxw
  ```

And for the statuses of the various MANA ranks/processes, do:
  ```bash
  $MANA_ROOT/bin/mana_status -l
  ```

### 3c. Restarting an MPI application

To restart the application, we do the following:

  ```bash
  $ mana_coordinator
  $ mpirun -np 1 mana_restart
  ```

By default, the mana_restart command looks for ckpt_rank_* folders in the
current directory to restart, and will fail if such folders cannot be found.
You can use the --restartdir flag of mana_restart to specify which directory
to look for such folders; more information can be found in Section C of the
MANA manpage.

Depending on whether there is another instance of mana_coordinator running, we
may need to either close all other instances or launch a coordinator with a
port different from the default 7779.

  ```bash
  $ mana_coordinator -p 7780
  ```

If the restart is successful, then you should see something similar to the
following printed:

  ```bash
  [17847] mtcp_restart.c:803 main:
    [Rank: 0] Choosing ckpt image: ./ckpt_rank_0/ckpt_mpi_hello_world.mana.exe_
      7c4af7b7e161f9fd-40000-1000f911e89fa5.dmtcp
  Signal received; continuing sleep for 294 seconds.
  ```

# Debugging internals of MANA:

For either command line with srun (or mpirun), prefix the command line
with `DMTCP_MANA_PAUSE=1` and run the command in the background:
  ```bash
  DMTCP_MANA_PAUSE=1 srun/mpirun ... &
  ```
It will stop with a printout about how to attach in GDB.  Execute that command.

Then:
  ```bash
  (gdb) p dummy=0
  ```

If you're debugging `mana_launch`:
  DMTCP_MANA_PAUSE=1 srun -n 1 `$MANA_ROOT/bin/mana_launch ...`
then it will drop you inside `mana_launch`.  To reach, for example,
`mpi-proxy-split/split-process.c:splitProcess()`, then do:
  ```bash
  (gdb) b execvp
  (gdb) continue
  (gdb) b splitProcess
  (gdb) continue
  ```
This is in a global constructor that executes before `main`.  To reach `main`
of the target executable, do `b main` instead.

Similarly for mana_restart, `DMTCP_MANA_PAUSE=1` drops you in
  `src/mtcp/mtcp_restart.c` shortly before `mtcp_restart.c:splitProcess()`.
If you want to reach the target executable, use `DMTCP_RESTART_PAUSE=1` instead.

When debugging in GDB, you can switch back and forth between upper half and
lower half programs as follows (assuming you're in `$MANA_ROOT`).
This is useful for examining both the upper and lower half stacks.
  ```bash
  (gdb) file bin/lh_proxy
  (gdb) file mpi-proxy-split/test/mpi_hello_world.mana.exe
  ```
If you had previously set a breakpoint before switching, then use:
  ```bash
  (gdb) set breakpoint always-inserted on
  ```
before switching among lower and upper halves.

When debugging with GDB after restart, if you are debugging inside the
target application, then a good strategy is:
  ```bash
  DMTCP_RESTART_PAUSE=1 srun -n 1 $MANA_ROOT/bin/mana_restart ...
  ```
It will pause, with instructions:
  ```bash
  (gdb) p dummy=0
  (gdb) source $MANA_ROOT/util/gdb-dmtcp-utils
  (gdb) add-symbol-files-all
  ```
The script `gdb-add-symbol-files-all` is a workaround that became necessary
since Linux 3.10, when Linux created a backwards-incompatible change
after which GDB has been failing to find the symbols for debugging.

# Building and testing on ordinary CentOS/Ubuntu (not the Cori supercomputer):

Before going into details, there are a few prerequisites:

1.  We have only tested with MPICH, not Open MPI.  Open MPI doesn't
    seem to support a statically linked MPI library.  (MANA uses a
    static MPI library in its lower half, while the MPI application continues
    to be linked dynamically  MANA's lower-half static MPI library can
    be replaced by a standard dynamic MPI library, but this would requires
    further work, and is not a current priority. In the future, we plan to
    remove the requirement of using a statically linked MPI to build MANA.)

2.  The MPICH package supplied with CentOS 7 provides `mpicc`, which
    accepts a `-static` flag.  But this is not sufficient for
    actual use.  To configure MANA to work with MPICH and `-static`, see:
      [README.mpich-static](lower-half/README.mpich-static).
    NOTE: It is planned for MANA to be based primarily on dynamic linking in the
    near future.

3.  Note `MANA_ROOT_DIR/contrib/mpi-proxy-split/Makefile_configure.in`
    for additional customization (for example, not Cori, not MPICH).

4.  OPTIONAL: Probably the `configure` above suffices.  But if `mpicc -show`
    indicates a flag `-lxml2` anyway, then this will create a dependency on
    libxml2.a.  Even if you have installed the package `libxml2`, then you
    _must_ also install `libxml2-dev` (libxml2-devel in CentOS) to get
    `libxml2.a`.
    (If you are missing `libxml2.a` and lack root privilege, try copying `libxml2.a`
     from some distro package, as described in [README.mpich-static](lower-half/README.mpich-static).)

## Building outside of Cori for MPICH:
 If you have satisfied the prerequisites above, then do `make -j mana`
and continue as for Cori, starting with the Cori instructions:

  ```bash
  $ git submodule update --init
  $ ./configure
  $ make -j mana
  ```

# Testing and Running without Slurm

Testing and running is mostly the same as described at the top.
However, at some HPC sites, you may need to replace `srun` with `mpirun`
in the two commands for mana_launch and mana_restart, above.

In a further complication, if using `mpirun` with MPICH-derived MPI
implementations, you may need to replace `srun` with:
  ```bash
  mpirun -iface ETHERNET_INTERFACE
  ```
You should find the correct definition of MPIRUN in:
  ```bash
  mpi-proxy-split/Makefile_config
  ```

NOTE: On HPC clusters, it is more common to use `srun`, along with a custom
      network interface (InfiniBand, Cray GNI, etc.).  So, most likely
      Ethernet will not be used, and libc.a will not call libnss_*.so.

The file `MANA_ROOT/configure-mana` tries to guess the Ethernet interface with:
  ```bash
  ip addr | grep -B1 link/ether  # and choose one of those interfaces
  ```
or else:
  ```bash
  ifconfig -a (deprecated)
  ```

If necessary, change the definition of `MPIRUN` in `configure-mana` for the
correct Ethernet interface on your compute node.  [ But this may no
longer be necessary. ]
