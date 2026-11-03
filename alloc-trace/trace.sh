#!/bin/bash
# Run a command with allocation tracing.
#   trace.sh [--mpi] [--real-cuda] [--dir DIR] command args...
# --mpi also preloads the MPI tracer (only for MPI programs).
# --real-cuda preloads the real libcuda and libcudart.  Needed when libmpi has
#   an RPATH to MANA's cuda-forward shims, which only work under MANA.
# Each traced process writes DIR/cuda-<host>-<pid>.csv (and mpi-...csv).
#
# Examples:
#   mpiexec -n 2 ./trace.sh --mpi --dir out ./app
#   ./trace.sh --dir out torchrun --nproc_per_node=2 ddp.py
here=$(cd "$(dirname "$0")" && pwd)
mpi=0
real=0
dir=.
while [ $# -gt 0 ]; do
  case "$1" in
    --mpi) mpi=1; shift ;;
    --real-cuda) real=1; shift ;;
    --dir) dir=$2; shift 2 ;;
    *) break ;;
  esac
done
mkdir -p "$dir"
export ALLOC_TRACE_DIR=$(cd "$dir" && pwd)
export CUDA_INJECTION64_PATH=$here/libcudatrace.so
if [ $mpi = 1 ]; then
  export LD_PRELOAD=$here/libmpitrace.so${LD_PRELOAD:+:$LD_PRELOAD}
fi
if [ $real = 1 ]; then
  # A soname is loaded once, so preloading these keeps the shims out.
  cuda=$(/sbin/ldconfig -p | awk '/libcuda\.so\.1 / {print $NF; exit}')
  cudart=$(ls "${CUDA_HOME:-/usr/local/cuda}"/lib64/libcudart.so.[0-9]* | head -1)
  if [ -z "$cuda" ] || [ -z "$cudart" ]; then
    echo "trace.sh: real libcuda.so.1 or libcudart not found" >&2
    exit 1
  fi
  export LD_PRELOAD=$cuda:$cudart${LD_PRELOAD:+:$LD_PRELOAD}
fi
exec "$@"
