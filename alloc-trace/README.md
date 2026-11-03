# alloc-trace

Traces how CUDA applications allocate GPU memory and which of it they hand to
MPI, to decide what MANA's VMM allocator has to handle.  Run applications
natively, not under MANA.

| File | What it does |
|---|---|
| `cudatrace.c` | CUPTI injection library (`CUDA_INJECTION64_PATH`): logs every allocation, free, VMM and IPC driver call. Works with shared and static cudart. |
| `mpitrace.c` | PMPI wrappers (`LD_PRELOAD`): logs the buffer addresses and sizes of MPI calls. |
| `trace.sh` | Sets both up and runs a command. |
| `analyze.py` | Summarizes a trace directory. |

## Build

    module load CUDA            # sets CUDA_HOME
    make                        # or: make CUDA_HOME=/path/to/cuda

`mpicc` must be the MPI the application uses.

## Run

MPI application (`--real-cuda` because this MPICH's RPATH points at MANA's
shims, which only work under MANA):

    mpiexec -n 2 ./trace.sh --mpi --real-cuda --dir out/app ./app args
    ./analyze.py out/app

PyTorch DDP (NCCL, so no `--mpi`):

    ./trace.sh --dir out/ddp torchrun --nproc_per_node=2 ddp.py
    PYTORCH_NO_CUDA_MEMORY_CACHING=1 ./trace.sh --dir out/ddp-raw torchrun ...

The first run shows what PyTorch's caching allocator asks the driver for; the
second shows the app's own requests.  `NCCL_DEBUG=INFO
NCCL_DEBUG_SUBSYS=COLL` logs NCCL's collectives and buffers.

GROMACS: GPU buffers only reach MPI with GPU-aware MPI enabled, e.g.
`GMX_ENABLE_DIRECT_GPU_COMM=1`, and a GROMACS built with `-DGMX_MPI=ON`.

Options of `analyze.py`: `--gran` (VMM granularity, 2M), `--small-limit`
(pool below this, 1M), `--ipc-threshold` (466019), `--per-proc`.

## Notes

* Only one CUPTI subscriber per process: no `nsys` or PyTorch profiler at the
  same time.
* `ALLOC_TRACE_CALLER=0` skips the stack walk that fills the `caller` column.
* Files are written per process as `cuda-<host>-<pid>.csv` and
  `mpi-<host>-<pid>.csv`; a process that crashes may lose its last 1 MB.
