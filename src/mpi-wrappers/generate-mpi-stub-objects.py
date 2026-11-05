#!/usr/bin/python3

# Writes the C definitions of Open MPI's predefined objects for libmpistub.so.
#   Usage: python3 <THIS_FILE> MPICC [ARGS...]
# where MPICC is the MPI library's C compiler, which preprocesses mpi.h.
#
# With Open MPI, a predefined handle is the address of an object in libmpi.so:
# MPI_COMM_WORLD is &ompi_mpi_comm_world.  libmpistub.so takes the place of
# libmpi.so in the upper half, so it must define these objects too.  Only
# their addresses matter.  With another MPI library, mpi.h declares no such
# object, and the output defines nothing.

import re
import subprocess
import sys

# The size of each type of object.  Open MPI pads its predefined objects to
# fixed sizes, the same in Open MPI 4.1 and 5.0.  A program may copy an
# object into its own memory when it is loaded (a copy relocation), and ld.so
# warns if the object of the library has another size.
SIZES = {
  "communicator": 0x200,
  "datatype": 0x200,
  "errhandler": 0x400,
  "file": 0x600,
  "group": 0x100,
  "info": 0x100,
  "instance": 0x200,
  "message": 0x100,
  "op": 0x800,
  "request": 0x100,
  "win": 0x200,
}
DEFAULT_SIZE = 0x100

# For example (attributes may come between 'extern' and 'struct'):
#   extern struct ompi_predefined_communicator_t ompi_mpi_comm_world;
DECLARATION = re.compile(
  r"\bextern\b[^;{}]*?\bstruct\s+ompi_predefined_(\w+)_t\s+(\w+)\s*;")

header = subprocess.run(sys.argv[1:] + ["-E", "-x", "c", "-"],
                        input="#include <mpi.h>\n", stdout=subprocess.PIPE,
                        universal_newlines=True, check=True).stdout
objects = {}
for match in DECLARATION.finditer(header):
  (kind, name) = match.groups()
  if kind not in SIZES:
    sys.stderr.write("generate-mpi-stub-objects.py: unknown type "
                     "ompi_predefined_%s_t of %s: size 0x%x\n" %
                     (kind, name, DEFAULT_SIZE))
  objects[name] = SIZES.get(kind, DEFAULT_SIZE)

print("// Open MPI's predefined objects; see generate-mpi-stub-objects.py.")
print("// This file does not include mpi.h, which declares them as structs.")
for name in sorted(objects):
  print("char %s[0x%x] __attribute__((aligned(64)));" % (name, objects[name]))
