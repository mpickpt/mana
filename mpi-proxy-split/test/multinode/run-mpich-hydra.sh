#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${MANA_TEST_HOSTFILE:-}" ]]; then
  echo "SKIP: MANA_TEST_HOSTFILE is not set"
  exit 0
fi

: "${MANA_HOME:?Set MANA_HOME to the built MANA tree}"
: "${MANA_TEST_MPICH_HOME:?Set MANA_TEST_MPICH_HOME}"

RANKS="${MANA_TEST_RANKS:-2}"
COORD_PORT="${MANA_TEST_COORD_PORT:-7780}"
TIMEOUT_SECONDS="${MANA_TEST_TIMEOUT:-300}"
REMOTE_USER="${MANA_TEST_REMOTE_USER:-${USER}}"
SSH_BIN="${MANA_TEST_SSH:-ssh}"
SCP_BIN="${MANA_TEST_SCP:-scp}"
WORK_ROOT="${MANA_TEST_WORKDIR:-/tmp/mana-multinode-${USER}}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

[[ "$RANKS" =~ ^[0-9]+$ && "$RANKS" -ge 2 &&
   "$COORD_PORT" =~ ^[0-9]+$ && "$COORD_PORT" -gt 0 &&
   "$COORD_PORT" -le 65535 && "$TIMEOUT_SECONDS" =~ ^[0-9]+$ &&
   "$TIMEOUT_SECONDS" -ge 10 ]] || {
  echo "ERROR: invalid rank count, port, or timeout" >&2
  exit 1
}
for path in "$MANA_HOME" "$MANA_TEST_MPICH_HOME" "$WORK_ROOT"; do
  [[ "$path" == /* && "$path" != *[!a-zA-Z0-9_./+-]* ]] || {
    echo "ERROR: use absolute paths without whitespace or shell characters" >&2
    exit 1
  }
done
[[ "$REMOTE_USER" != *[!a-zA-Z0-9_.-]* ]] || exit 1
[[ -r "$MANA_TEST_HOSTFILE" ]] || {
  echo "ERROR: cannot read hostfile: $MANA_TEST_HOSTFILE" >&2
  exit 1
}
mapfile -t NODES < <(
  sed -e 's/#.*$//' -e 's/\r$//' "$MANA_TEST_HOSTFILE" |
    awk 'NF {host=$1; sub(/:.*/, "", host);
             if (!seen[host]++) print host}'
)
[[ "${#NODES[@]}" -ge 2 ]] || {
  echo "ERROR: the test requires at least two distinct hosts" >&2
  exit 1
}
for host in "${NODES[@]}"; do
  [[ "$host" != *[!a-zA-Z0-9_.-]* ]] || exit 1
done

MPICC="$MANA_TEST_MPICH_HOME/bin/mpicc"
MPIEXEC="$MANA_TEST_MPICH_HOME/bin/mpiexec"
DMTCP_COORD="$MANA_HOME/bin/dmtcp_coordinator"
DMTCP_COMMAND="$MANA_HOME/bin/dmtcp_command"
for executable in "$MPICC" "$MPIEXEC" "$DMTCP_COORD" "$DMTCP_COMMAND" \
                  "$MANA_HOME/bin/mana_launch" \
                  "$MANA_HOME/bin/mana_restart" "$MANA_HOME/bin/lower-half"; do
  [[ -x "$executable" ]] || {
    echo "ERROR: missing executable: $executable" >&2
    exit 1
  }
done

mkdir -p "$WORK_ROOT"
WORKDIR="$(mktemp -d "$WORK_ROOT/run.XXXXXXXX")"
RUN_ID="$(basename "$WORKDIR")"
TEST_HOME="$WORKDIR/home"
APP="$WORKDIR/mpi_counter"
HOSTFILE="$WORKDIR/hosts.txt"
CKPTDIR="$WORKDIR/checkpoints"
CKPT_TMP="$WORKDIR/tmp"
LOGDIR="$WORKDIR/logs"
STATUS_FILE="$TEST_HOME/.mana.rc"
LAUNCH_LOG="$WORKDIR/launch.log"
RESTART_LOG="$WORKDIR/restart.log"
COORD_OWNED=0
COORD_SERIAL=0
mkdir -p "$TEST_HOME" "$CKPTDIR" "$CKPT_TMP" "$LOGDIR"

quote() {
  python3 -c 'import shlex, sys; print(shlex.quote(sys.argv[1]))' "$1"
}

remote() {
  local host="$1"
  shift
  timeout --kill-after=5 "$TIMEOUT_SECONDS" \
    "$SSH_BIN" -n -o BatchMode=yes -o ConnectTimeout=10 \
    "${REMOTE_USER:+$REMOTE_USER@}$host" "$@"
}

copy_to() {
  local source="$1" host="$2" destination="$3" expected actual
  expected="$(sha256sum "$source" | awk '{print $1}')"
  actual="$(remote "$host" "if test -f '$destination'; then
    sha256sum '$destination'; fi" | awk '{print $1}')"
  # Shared storage (or the local host) may already expose this exact file.
  [[ "$actual" == "$expected" ]] && return 0
  timeout --kill-after=5 "$TIMEOUT_SECONDS" \
    "$SCP_BIN" -q -o BatchMode=yes -o ConnectTimeout=10 "$source" \
    "${REMOTE_USER:+$REMOTE_USER@}$host:$destination" </dev/null
}

read_status() {
  coord_host="$(awk '/^Host:/ {print $2; exit}' "$STATUS_FILE")"
  coord_port="$(awk '/^Port:/ {print $2; exit}' "$STATUS_FILE")"
  [[ -n "$coord_host" && "$coord_port" == "$COORD_PORT" ]]
}

command_to_coordinator() {
  read_status
  timeout --kill-after=2 10 "$DMTCP_COMMAND" \
    --coord-host "$coord_host" --coord-port "$coord_port" "$@"
}

stop_coordinator() {
  if [[ "$COORD_OWNED" -eq 1 && -s "$STATUS_FILE" ]]; then
    command_to_coordinator --kill >/dev/null 2>&1 || true
    command_to_coordinator --quit >/dev/null 2>&1 || true
  fi
  COORD_OWNED=0
}

# Only processes carrying this run's exact environment tag are eligible.
CLEANUP_CODE="$(cat <<'PY_CLEANUP'
import os
from pathlib import Path
import signal
import sys
import time

tag = ("MANA_MULTINODE_RUN_ID=" + sys.argv[1]).encode()

def belongs(pid):
    try:
        p = Path("/proc") / str(pid)
        return (p.stat().st_uid == os.getuid() and
                tag in (p / "environ").read_bytes().split(b"\0"))
    except OSError:
        return False

pids = [int(p.name) for p in Path("/proc").iterdir()
        if p.name.isdigit() and int(p.name) != os.getpid()]
for sig in (signal.SIGTERM, signal.SIGKILL):
    for pid in pids:
        if belongs(pid):
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
    if sig == signal.SIGTERM:
        time.sleep(0.2)
PY_CLEANUP
)"

finish() {
  local result="$1"
  trap - EXIT INT TERM
  set +e
  stop_coordinator
  for host in "${NODES[@]}"; do
    remote "$host" "python3 -c $(quote "$CLEANUP_CODE") '$RUN_ID'"
  done
  python3 -c "$CLEANUP_CODE" "$RUN_ID"
  if [[ "$result" -ne 0 ]]; then
    for log in "$WORKDIR"/*.log; do
      [[ -f "$log" ]] && tail -n 25 "$log" >&2
    done
  fi
  echo "Evidence directory: $WORKDIR"
  exit "$result"
}
trap 'finish $?' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

start_coordinator() {
  rm -f "$STATUS_FILE"
  COORD_SERIAL=$((COORD_SERIAL + 1))
  (
    cd "$WORKDIR"
    "$DMTCP_COORD" --port "$COORD_PORT" --interval 0 \
      --ckptdir "$CKPTDIR" --daemon --stale-timeout -1 \
      --coord-logfile "$WORKDIR/coordinator-$COORD_SERIAL.log" \
      --status-file "$STATUS_FILE"
  )
  COORD_OWNED=1
  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  until [[ -s "$STATUS_FILE" ]]; do
    [[ "$SECONDS" -lt "$deadline" ]] || return 1
    sleep 1
  done
  read_status
  for host in "${NODES[@]}"; do
    copy_to "$STATUS_FILE" "$host" "$STATUS_FILE"
  done
}

peers_running() {
  local status
  status="$(command_to_coordinator --status 2>/dev/null)" || return 1
  [[ "$(awk -F= '/NUM_PEERS=/ {gsub(/ /,"",$2);print $2}' \
      <<<"$status")" == "$RANKS" &&
     "$(awk -F= '/RUNNING=/ {gsub(/ /,"",$2);print $2}' \
      <<<"$status")" == yes ]]
}

# The same parser checks native execution, progress, and the restored state.
check_logs() {
  python3 - "$@" <<'PY_CHECK'
import json
from pathlib import Path
import re
import sys

mode, path, ranks = sys.argv[1:4]
ranks = int(ranks)
pattern = re.compile(r"rank=(\d+)/(\d+) host=(\S+).*?"
                     r"iteration=(\d+) allreduce_sum=(-?\d+)")

def parse(path):
    records = {rank: [] for rank in range(ranks)}
    for line in Path(path).read_text(errors="replace").splitlines():
        match = pattern.search(line)
        if not match:
            continue
        rank, size, host, iteration, total = match.groups()
        rank, size, iteration, total = map(int, (rank, size, iteration, total))
        expected = ranks * (2 * iteration) + ranks * (ranks - 1) // 2
        if rank not in records or size != ranks or total != expected:
            raise SystemExit("Invalid MPI record: " + line)
        records[rank].append((iteration, host))
    if any(not values for values in records.values()):
        raise SystemExit("Waiting for records from every rank")
    if len({values[0][1] for values in records.values()}) < 2:
        raise SystemExit("Ranks must execute on at least two distinct hosts")
    return records

records = parse(path)
last = {str(rank): values[-1][0] for rank, values in records.items()}
if mode == "snapshot":
    print(json.dumps(last, sort_keys=True))
elif mode == "progress":
    reference = json.loads(Path(sys.argv[4]).read_text())
    delta = int(sys.argv[5])
    if any(last[str(rank)] < int(reference[str(rank)]) + delta
           for rank in range(ranks)):
        raise SystemExit("Waiting for progress from every rank")
elif mode == "verify":
    after = parse(sys.argv[4])
    floor = json.loads(Path(sys.argv[5]).read_text())
    for rank in range(ranks):
        first, end = after[rank][0][0], after[rank][-1][0]
        before = last[str(rank)]
        if not int(floor[str(rank)]) <= first < before:
            raise SystemExit("Rollback not demonstrated for rank " + str(rank))
        if end <= before:
            raise SystemExit("No resumed forward progress for rank " + str(rank))
        print("rank %d: checkpoint floor %s, before kill %d, restart %d..%d" %
              (rank, floor[str(rank)], before, first, end))
    print("All ranks resumed checkpointed state and correct MPI_Allreduce")
else:
    raise SystemExit("Unknown log check")
PY_CHECK
}

wait_progress() {
  local path="$1" reference="$2" delta="$3"
  local deadline=$((SECONDS + TIMEOUT_SECONDS))
  until check_logs progress "$path" "$RANKS" "$reference" "$delta" \
      >"$WORKDIR/progress.log" 2>&1; do
    [[ "$SECONDS" -lt "$deadline" ]] || return 1
    sleep 1
  done
}

INVENTORY_CODE="$(cat <<'PY_INVENTORY'
import os
from pathlib import Path
import sys

root, token = Path(sys.argv[1]), sys.argv[2]
if any(root.rglob("*.temp")) or any(root.rglob("*.tmp")):
    raise SystemExit("Incomplete checkpoint files remain")
images = set()
rows = []
for directory in root.glob("ckpt_rank_*"):
    rank = directory.name.removeprefix("ckpt_rank_")
    if not rank.isdigit() or directory.is_symlink():
        raise SystemExit("Invalid rank directory")
    files = list(directory.glob("ckpt_*.dmtcp"))
    header = directory / "header.mana"
    if (len(files) != 1 or not header.is_file() or header.is_symlink() or
            not files[0].is_file() or files[0].is_symlink()):
        raise SystemExit("Incomplete rank image/header")
    image, meta = files[0].stat(), header.stat()
    if image.st_size <= 0 or meta.st_size <= 0:
        raise SystemExit("Empty rank image/header")
    images.add(str(files[0].resolve()))
    rows.append("|".join(map(str, (rank, image.st_size, image.st_mtime_ns,
                                 image.st_ino, meta.st_size, meta.st_mtime_ns))))
tag = ("MANA_MULTINODE_RUN_ID=" + token).encode()
for proc in Path("/proc").iterdir():
    if not proc.name.isdigit():
        continue
    try:
        tagged = tag in (proc / "environ").read_bytes().split(b"\0")
    except OSError:
        continue
    if not tagged:
        continue
    try:
        for fd in (proc / "fd").iterdir():
            if os.readlink(fd) not in images:
                continue
            text = (proc / "fdinfo" / fd.name).read_text()
            flags = next(line.split()[1] for line in text.splitlines()
                         if line.startswith("flags:"))
            if int(flags, 8) & os.O_ACCMODE:
                raise SystemExit("A tagged process still has an image writer")
    except (FileNotFoundError, ProcessLookupError):
        continue
    except PermissionError:
        raise SystemExit("Cannot inspect this run's checkpoint writers")
print("\n".join(sorted(rows)))
PY_INVENTORY
)"

checkpoint_inventory() {
  : >"$WORKDIR/inventory.txt"
  for host in "${NODES[@]}"; do
    remote "$host" "python3 -c $(quote "$INVENTORY_CODE") \
      '$CKPTDIR' '$RUN_ID'" >>"$WORKDIR/inventory.txt" || return 1
  done
  python3 - "$WORKDIR/inventory.txt" "$RANKS" <<'PY_RANKS'
from pathlib import Path
import sys

rows = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    if not line:
        continue
    rank = int(line.split("|")[0])
    if rank in rows and rows[rank] != line:
        raise SystemExit("Conflicting images for rank " + str(rank))
    rows[rank] = line
if set(rows) != set(range(int(sys.argv[2]))):
    raise SystemExit("Waiting for exactly one image/header per rank")
print("\n".join(rows[rank] for rank in sorted(rows)))
PY_RANKS
}

sed -e 's/#.*$//' -e 's/\r$//' -e '/^[[:space:]]*$/d' \
  "$MANA_TEST_HOSTFILE" >"$HOSTFILE"
for host in "${NODES[@]}"; do
  remote "$host" "set -eu
    command -v python3 >/dev/null
    test -x '$MANA_HOME/bin/mana_launch'
    test -x '$MANA_HOME/bin/mana_restart'
    test -x '$MANA_HOME/bin/dmtcp_command'
    test -x '$MANA_TEST_MPICH_HOME/bin/hydra_pmi_proxy'
    mkdir -p '$TEST_HOME' '$CKPTDIR' '$CKPT_TMP' '$LOGDIR'"
  for binary in bin/lower-half lib/dmtcp/libmana.so \
                lib/dmtcp/libdmtcp_ipc.so; do
    expected="$(sha256sum "$MANA_HOME/$binary" | awk '{print $1}')"
    actual="$(remote "$host" "sha256sum '$MANA_HOME/$binary'" |
      awk '{print $1}')"
    [[ "$actual" == "$expected" ]] || {
      echo "ERROR: different MANA runtime on $host: $binary" >&2
      exit 1
    }
  done
  copy_to "$HOSTFILE" "$host" "$HOSTFILE"
done
"$MPICC" -O2 -g -Wall -Wextra -Wpedantic \
  "$SCRIPT_DIR/mpi_counter.c" -o "$APP"
for host in "${NODES[@]}"; do
  copy_to "$APP" "$host" "$APP"
  remote "$host" "chmod 755 '$APP'"
done

RUNTIME_LIBS="$MANA_TEST_MPICH_HOME/lib:$MANA_HOME/lib"
RUNTIME_LIBS+=":$MANA_HOME/lib/dmtcp:${LD_LIBRARY_PATH:-}"
MPI_COMMON=("$MPIEXEC" -launcher ssh -launcher-exec "$SSH_BIN"
  -f "$HOSTFILE" -n "$RANKS"
  -genv HOME "$TEST_HOME" -genv MANA_MULTINODE_RUN_ID "$RUN_ID"
  -genv PATH "$MANA_HOME/bin:$MANA_TEST_MPICH_HOME/bin:$PATH"
  -genv LD_LIBRARY_PATH "$RUNTIME_LIBS"
  -genv HYDRA_LAUNCHER ssh -genv HYDRA_ENV all)
MPI_BASE=("${MPI_COMMON[@]}" -wdir "$WORKDIR")

echo "[1/7] Native MPICH/Hydra smoke"
timeout --kill-after=5 "$TIMEOUT_SECONDS" \
  env MANA_MULTINODE_RUN_ID="$RUN_ID" "${MPI_BASE[@]}" \
  "$APP" 5 "$LOGDIR" "$RANKS" >"$WORKDIR/native.log" 2>&1
check_logs snapshot "$WORKDIR/native.log" "$RANKS" >/dev/null

echo "[2/7] Start DMTCP coordinator directly"
start_coordinator
echo "[3/7] Finite MANA smoke"
timeout --kill-after=5 "$TIMEOUT_SECONDS" \
  env MANA_MULTINODE_RUN_ID="$RUN_ID" "${MPI_BASE[@]}" \
  "$MANA_HOME/bin/mana_launch" --ckptdir "$CKPTDIR" --tmpdir "$CKPT_TMP" \
  "$APP" 5 "$LOGDIR" "$RANKS" >"$WORKDIR/finite.log" 2>&1
check_logs snapshot "$WORKDIR/finite.log" "$RANKS" >/dev/null
stop_coordinator
sleep 1
start_coordinator

echo "[4/7] Distributed MANA launch"
env MANA_MULTINODE_RUN_ID="$RUN_ID" "${MPI_BASE[@]}" \
  "$MANA_HOME/bin/mana_launch" --ckptdir "$CKPTDIR" --tmpdir "$CKPT_TMP" \
  "$APP" 0 "$LOGDIR" "$RANKS" >"$LAUNCH_LOG" 2>&1 &
python3 - "$RANKS" >"$WORKDIR/zero.json" <<'PY_ZERO'
import json
import sys
print(json.dumps({str(rank): 0 for rank in range(int(sys.argv[1]))}))
PY_ZERO
wait_progress "$LAUNCH_LOG" "$WORKDIR/zero.json" 5
check_logs snapshot "$LAUNCH_LOG" "$RANKS" >"$WORKDIR/checkpoint-floor.json"

echo "[5/7] Blocking checkpoint and complete-image validation"
read_status
timeout --kill-after=5 "$TIMEOUT_SECONDS" "$DMTCP_COMMAND" \
  --coord-host "$coord_host" --coord-port "$coord_port" --bcheckpoint
deadline=$((SECONDS + TIMEOUT_SECONDS))
previous=""
while true; do
  current=""
  if peers_running && current="$(checkpoint_inventory)"; then
    [[ -n "$current" && "$current" == "$previous" ]] && break
  fi
  previous="$current"
  [[ "$SECONDS" -lt "$deadline" ]] || exit 1
  sleep 2
done
check_logs snapshot "$LAUNCH_LOG" "$RANKS" >"$WORKDIR/after-checkpoint.json"
# Deliberate progress after the checkpoint makes rewind observable.
wait_progress "$LAUNCH_LOG" "$WORKDIR/after-checkpoint.json" 3

echo "[6/7] Kill and restart"
stop_coordinator
sleep 1
check_logs snapshot "$LAUNCH_LOG" "$RANKS" >"$WORKDIR/before-kill.json"
start_coordinator
MPI_RESTART=("${MPI_COMMON[@]}" -wdir "$CKPTDIR")
env MANA_MULTINODE_RUN_ID="$RUN_ID" "${MPI_RESTART[@]}" \
  "$MANA_HOME/bin/mana_restart" --verbose \
  --ckptdir "$CKPTDIR" --tmpdir "$CKPT_TMP" --restartdir "$CKPTDIR" \
  >"$RESTART_LOG" 2>&1 &
wait_progress "$RESTART_LOG" "$WORKDIR/before-kill.json" 2

echo "[7/7] Verify every rank's rollback, collective results, and progress"
check_logs verify "$LAUNCH_LOG" "$RANKS" "$RESTART_LOG" \
  "$WORKDIR/checkpoint-floor.json" | tee "$WORKDIR/result.log"
echo "PASS: multi-node MPICH/Hydra checkpoint-restart validation"
