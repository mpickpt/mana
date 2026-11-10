---
title: MPI wrappers
parent: Developer guide
nav_order: 4
description: "How an MPI wrapper of MANA works, the generated files, and how to add a wrapper."
---

# MPI wrappers
{: .no_toc }

1. TOC
{:toc}

---

## The wrapper files

The wrappers are in `src/mpi-wrappers/`. Each file holds the wrappers of one
area of MPI:

| File | MPI functions |
|---|---|
| `mpi_wrappers.cpp` | `MPI_Init`, `MPI_Init_thread`, `MPI_Finalize`, and other general calls |
| `mpi_p2p_wrappers.cpp` | Point-to-point: send, receive, probe |
| `mpi_request_wrappers.cpp` | `MPI_Wait*`, `MPI_Test*`, `MPI_Request_get_status` |
| `mpi_collective_wrappers.cpp` | Blocking and non-blocking collectives |
| `mpi_comm_wrappers.cpp` | Communicators, attributes and keyvals |
| `mpi_group_wrappers.cpp` | Groups |
| `mpi_type_wrappers.cpp` | Datatypes |
| `mpi_op_wrappers.cpp` | User-defined operations |
| `mpi_cart_wrappers.cpp` | Cartesian topologies |
| `mpi_file_wrappers.cpp` | MPI-IO |
| `mpi_win_wrappers.cpp` | `MPI_Alloc_mem` and `MPI_Free_mem` |
| `mpi_error_wrappers.cpp` | Error classes and strings |
| `mpi_t_wrappers.cpp` | The `MPI_T` tools interface |
| `mpi_fortran_attr_wrappers.cpp` | The Fortran bindings of attributes, which the generator cannot make |

All wrappers compile into the static library `libmpiwrappers.a`. MANA links
the whole library into `libmana.so`.

### Generated files

Python scripts make these files from input files. `make` runs the scripts. Do
not edit the output files. Edit the input file or the script.

| Output | Script | Input | Contents |
|---|---|---|---|
| `mpi_fortran_wrappers.cpp` | `generate-mpi-fortran-wrappers.py` | `mpi_fortran_wrappers.txt` | The Fortran bindings, which call the C wrappers |
| `mpi_unimplemented_wrappers.cpp` | `generate-mpi-unimplemented-wrappers.py` | `mpi_unimplemented_wrappers.txt` | A wrapper that stops the program, for each MPI function that MANA does not support |
| `mpi_stub_wrappers.c` | `generate-mpi-stub-wrappers.py` | `mpi_stub_wrappers.txt` | The functions of `libmpistub.so` |
| `mpi_stub_objects.c` | `generate-mpi-stub-objects.py` | `mpi.h` | The predefined objects of Open MPI, for `libmpistub.so` |

### The stub library and the audit module

`libmpistub.so` takes the place of the MPI library in the upper half (see
[Architecture](architecture.html#the-stub-mpi-library)). It has one function
for each MPI function, and each function only aborts. No program calls them,
because `libmana.so` comes first in the search order. `mpi_stub_data.c` defines
the variables that `mpi.h` declares, such as `MPI_F_STATUS_IGNORE`.

`make` builds `libmpistub_audit.so` from `mpi_stub_audit.c`. It is an audit module
of `ld.so`, which loads `libmpistub.so` in place of each MPI library. It is
built without libc, because an audit module gets its own namespace, and a
second libc there would call `mmap()` without the wrappers of MANA. `make`
builds the module with the C compiler that `configure` found (`CC`), and the
build fails if the module depends on a shared library. On a Cray system, do
not use `cc` for it. The Cray compiler wrapper can add a shared library, such
as `libxpmem.so`, also with `-nostdlib`.

## The parts of a wrapper

This wrapper from `mpi_type_wrappers.cpp` shows the usual parts:

```cpp
#pragma weak MPI_Type_contiguous = PMPI_Type_contiguous
int PMPI_Type_contiguous(int count, MPI_Datatype oldtype,
                         MPI_Datatype *newtype)
{
  int retval;
  LOWER_HALF_DISABLE_CKPT();                                     // 1
  MPI_Datatype real_datatype =
    get_real_id((mana_mpi_handle){.datatype = oldtype}).datatype; // 2
  JUMP_TO_LOWER_HALF(lh_info->fsaddr);                           // 3
  retval = NEXT_FUNC(Type_contiguous)(count, real_datatype, newtype);
  RETURN_TO_UPPER_HALF();                                        // 4
  if (retval == MPI_SUCCESS) {                                   // 5
    mana_datatype_desc *desc = alloc_datatype_desc();
    desc->constructor = MANA_TYPE_CONTIGUOUS;
    desc->count = count;
    desc->oldtype = oldtype;
    *newtype = new_virt_datatype(*newtype, desc);
  }
  LOWER_HALF_ENABLE_CKPT();                                      // 6
  return retval;
}
```

1. `LOWER_HALF_DISABLE_CKPT()` blocks checkpoints until step 6. If a
   checkpoint is in progress, the thread waits here. See
   [Keep threads out of the lower half](checkpoint-internals.html#keep-threads-out-of-the-lower-half).
2. `get_real_id()` translates the virtual handle of the application to the
   real handle.
3. `JUMP_TO_LOWER_HALF` sets the TLS of the lower half. `NEXT_FUNC` gets the
   real MPI function from the function table of the lower half.
4. `RETURN_TO_UPPER_HALF` sets the TLS of the upper half again.
5. The wrapper makes a descriptor and a virtual handle for the new datatype.
   A restart uses the descriptor to make the datatype again.
6. `LOWER_HALF_ENABLE_CKPT()` lets checkpoints start again.

The wrapper defines `PMPI_Type_contiguous`, and `MPI_Type_contiguous` is a weak
alias of it. So a PMPI tool that `MANA_PRELOAD` loads can intercept
`MPI_Type_contiguous` and call `PMPI_Type_contiguous`.

### Rules for a wrapper

- **Translate each input handle.** If a virtual handle goes to the lower half,
  the MPI library gets an invalid handle.
- **Make a virtual handle for each output handle.** If the application gets a
  real handle, the next wrapper cannot translate it, and a restart cannot make
  the object again.
- **Translate predefined values that MANA does not virtualize.** For
  `MPI_Info` and `MPI_Errhandler`, use `lower_half_handle()` and
  `upper_half_handle()`.
- **Keep the lower-half call between `LOWER_HALF_DISABLE_CKPT()` and
  `LOWER_HALF_ENABLE_CKPT()`.** Also keep the changes of upper-half state
  that go with the call (virtual handles, the log of pending requests) between
  them. A checkpoint between the call and the change would save a wrong state.
- **Count collective calls.** A collective wrapper calls `commit_begin(comm)`
  before the lower-half call and `commit_finish(comm)` after it. The
  Collective Clock depends on this.
- **Mark the requests of non-blocking collectives.** A non-blocking
  collective wrapper makes its request with `new_virt_collective_request()`.
  The checkpoint completes these requests before it saves the image.
- **Get the gid seed of a new communicator before the lower-half call.** A
  wrapper that makes a communicator calls `child_gid_seed(comm)` before the
  lower-half call. For `MPI_Comm_create_group`, it calls
  `group_child_gid_seed()`. It gives the seed to `new_virt_comm()`. Get the
  seed on each member, also on a member that gets `MPI_COMM_NULL`, because the
  seed counts the calls on the parent communicator. See
  [The gid of a communicator](virtual-ids.html#the-gid-of-a-communicator).
- **Use the existing point-to-point wrappers as a model.** A point-to-point
  wrapper must count messages, look in the buffer of MANA, and record pending
  requests for the drain. Copy the pattern of the nearest existing wrapper.

## Add a wrapper

To support an MPI function that MANA does not support yet, do these steps:

1. **Remove the function from `mpi_unimplemented_wrappers.txt`.**
2. **Add the function to the function table of the lower half**, if it is not
   there. The table is the macro `FOREACH_FNC` in
   `src/lower-half/lower-half-api.h`. `NEXT_FUNC(Name)` finds `MPI_Name` with
   it.
3. **Write the wrapper** in the file of its area. Follow the rules above.
4. **If the function makes a new object**, record what a restart needs in the
   descriptor of the object, and make the object again in
   `reconstruct_descriptors()` (`src/virtual_id.cpp`). See
   [Virtual handles](virtual-ids.html).
5. **Add the Fortran binding** if Fortran programs need the function. Add the C
   prototype to `mpi_fortran_wrappers.txt`. The generator makes the Fortran
   function, which calls the C wrapper.
6. **Check that `mpi_stub_wrappers.txt` declares the function.** Otherwise,
   a program that calls it does not load.
7. **Add a test** in `test/`. See [Testing](testing.html).
8. **Build and test:** `make && make check`.
