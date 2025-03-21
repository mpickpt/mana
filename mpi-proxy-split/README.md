# General notes for building, testing and running on Perlmutter supercomputer at NERSC:

The main pointers for background information on MANA are:

* MANA at github:
  https://github.com/mpickpt/mana

* MANA documentation:
  https://mana-doc.readthedocs.io/en/latest/

The main scripts for this:
  * `mana_launch.py`
  * `mana_restart.py`
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

   Run the following to update the DMTCP submodule needed by MANA.  Then configure and build.
   ```bash
   $ git submodule update --init
   $ ./configure
   $ make -j mana
   ```

   If you need to alter some configure options, you can edit and execute ./configure-mana.

## 3. Testing MANA

   For convenience, add the MANA bin directory to your PATH.

   ```bash
   $ export PATH=$PATH:$MANA_ROOT/bin
   ```

   Optionally, you can make this path persistent by doing:

   ```bash
   $ echo 'export PATH=$PATH:$MANA_ROOT/bin' >> ~/.bashrc
   ```

   On clusters, we need to allocate resources to run a job. The command below
can be modified according to your needs.

   ```bash
   $ salloc -N 1 -C cpu -q interactive -t 01:00:00
   ```

There are many MPI test programs to use for testing MANA in the directory
$MANA_ROOT/mpi-proxy-split/test.

### 3a. Launching an MPI application

The MANA directory comes with many test MPI applications that can be found in
mpi-proxy-plugin/test. Depending on the application, you may require more than
one MPI process running -- for example, `ping_pong.exe` requires two. To
support this, change the argument after -np accordingly. For this tutorial,
we'll use `mpi_hello_world.exe`, which can run with one MPI process.

In the test directory, you can find compiled binaries ending with `.exe` and
`.mana.exe`. Binaries end with `.exe` are compiled and linked with the native
MPI library and can run independtly from MANA. Binaries end with `.mana.exe`
are linked with MANA's stub libraries. They can only run with MANA.

Depends on the cluster and MPI library, you may need either `mpirun` or `srun`
to launch an MPI application with mutiple processes. Here, we use `mpirun` as
an example.

  ```bash
  $ mana_coordinator
  $ mpirun -n 1 mana_launch.py mpi-proxy-split/test/mpi_hello_world.exe
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
      contrib/mpi-proxy-split/test/mpi_hello_world.exe &
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
  Signal received; continuing sleep for 294 seconds.
  ```

# Debugging internals of MANA:

You can use the `--gdb` flag of `mana_launch.py` to launch MANA in gdb.
  ```bash
  mpirun -n 1 $MANA_ROOT/bin/mana_launch.py --gdb
  ```
Then you can use `b main` to create a breakpoint at the main function.

Similarly for mana_restart, you can use the `--gdb` flag.

If symbols of the upper-half program is missing in GDB, you can use the
`add-symbol-files-all` command provided in `gdb-dmtcp-utils` to add them
to the debugging session.
  ```bash
  (gdb) source $MANA_ROOT/util/gdb-dmtcp-utils
  (gdb) add-symbol-files-all
  ```
The script `gdb-add-symbol-files-all` is a workaround that became necessary
since Linux 3.10, when Linux created a backwards-incompatible change
after which GDB has been failing to find the symbols for debugging.
