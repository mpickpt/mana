#!/usr/bin/env python3
import sys
import os
import shutil

help_msg = '''
  USAGE: [srun] mana_launch [--verbose] [--timing] [DMTCP_OPTIONS ...]
                                         [--ckptdir DIR] MPI_EXECUTABLE [...]
          For DMTCP options, do: dmtcp_launch --help
'''

# dmtcp options that requires a value
dmtcp_options = ["-i", "--interval", "-h", "--coord-host",
                 "-p", "--coord-port", "--ckptdir",
                 "--ckpt-signal", "--with-plugin",
                 "--tmpdir", "--coord-logfile"]
verbose = False
gdb = False
mana_root_path = os.path.dirname(os.path.realpath(__file__)) + "/../"

# if no arguments provided, print the help message
if not sys.argv:
  print(help_msg)
  sys.exit(1)

# Find where are the target app and its arguments
index = 1
while index < len(sys.argv):
  if sys.argv[index] in dmtcp_options:
    index = index + 2
  elif sys.argv[index][0] == '-':
    index = index + 1
  else:
    break;
dmtcp_flags = sys.argv[1:index]
target_app = sys.argv[index:]

# Get the absolute path of the target app
tmp = target_app[0]
target_app[0] = shutil.which(target_app[0])
if not target_app[0]:
  print("Executable " + tmp + " not found.")
  sys.exit(1)

# Special arguments
if "--gdb" in dmtcp_flags:
  gdb = True
  dmtcp_flags.remove("--gdb")
if "--help" in dmtcp_flags:
  print(help_msg)
  sys.exit(1)
if "--verbose" in dmtcp_flags:
  verbose = True
  dmtcp_flags.remove("--verbose")
if "--timing" in dmtcp_flags:
  os.environ["MANA_TIMING"] = "1"
  dmtcp_flags.remove("--timing")
if "srun" in target_app or "sbatch" in target_app:
  print(help_msg)
  sys.exit(1)
if "--ckptdir" in dmtcp_flags:
  ckptdir_path = dmtcp_flags[dmtcp_flags.index("--ckptdir") + 1]
  if not os.path.exists(ckptdir_path):
    print("mana_launch: --ckptdir " + ckptdir_path + ": Checkpoint directory doesn't exist")
    sys.exit(1)
if "--quiet" in dmtcp_flags or "-q" in dmtcp_flags:
  os.environ["MANA_QUIET"] = "1"
  if "--quiet" in dmtcp_flags:
    dmtcp_flags.remove("--quiet")
  else:
    dmtcp_flags.remove("-q")

# # At higher ranks, restarting NIMROD causes a heap overflow (for a more detailed
# # write up, refer to Section 4 of mpi-proxy-split/doc/nimrod-build-tutorial.txt).
# # A temporary workaround is to swap the memory addresses of NIMROD and lh_proxy,
# # which requires a specially built version of lh_proxy.
# if echo $target_app | grep -q nimrod ; then
#   export USE_LH_PROXY_DEFADDR=1
# fi

# Find the .mana.rc file to get the hostname and port of the coordinator
rc_filename = ""
host_flag = ""
port_flag = ""
if "SLURM_JOB_ID" in os.environ:
  rc_filename = os.path.expanduser("~/.mana-slurm-" + os.environ["SLURM_JOB_ID"] + ".rc")
else:
  rc_filename = os.path.expanduser("~/.mana.rc")
rc_file = open(rc_filename)
for line in rc_file:
  line = line.split()
  if not line:
    break
  if line[0] == "Host:":
    host_flag = " -h " + line[1] + " "
  elif line[0] == "Port:":
    port_flag = " -p " + line[1] + " "
rc_file.close()

# Remove old ~/.mana_*.rc files from a week ago or more.
# os.system("find $HOME/.mana*.rc -ignore_readdir_race -maxdepth 0 -mtime +7 -type f -delete")

# Check dmtcp coordinator
coordinator_found = os.system(mana_root_path + "bin/dmtcp_command -s " + host_flag + port_flag + " &>/dev/null")
if coordinator_found != 0:
  print("No MANA coordinator detected. Try:")
  print("  " + mana_root_path + "bin/dmtcp_coordinator")
  print("Or:")
  print("  " + mana_root_path + "bin/dmtcp_coordinator --exit-on-last -q --daemon")
  sys.exit(1)

# Build the command line to be executed
if not verbose:
  dmtcp_flags.insert(0, "-q -q")
if gdb:
  cmd_line = shutil.which("gdb") + " --args "
else:
  cmd_line = ""
cmd_line += mana_root_path + "bin/dmtcp_launch --mpi" + host_flag + port_flag + \
            "--no-gzip --join-coordinator --disable-dl-plugin" + \
            " --with-plugin " + mana_root_path + "lib/dmtcp/libmana.so " + \
            " ".join(dmtcp_flags) + " " + mana_root_path + "bin/kernel-loader " + \
            " " .join(target_app)
if verbose:
  print(cmd_line)

# This is for debug only
# cmd_line = "/usr/bin/echo " + cmd_line
os.execv(cmd_line.split()[0], cmd_line.split())
