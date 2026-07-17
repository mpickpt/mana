#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${MANA_TEST_HOSTFILE:-}" ]]; then
    echo "SKIP: MANA_TEST_HOSTFILE is not set"
    exit 0
fi

: "${MANA_HOME:?Set MANA_HOME to the built MANA tree}"
: "${MANA_TEST_MPICH_HOME:?Set MANA_TEST_MPICH_HOME}"

REMOTE_USER="${MANA_TEST_REMOTE_USER:-${USER}}"
RANKS="${MANA_TEST_RANKS:-2}"
COORD_PORT="${MANA_TEST_COORD_PORT:-7780}"
WORKDIR="${MANA_TEST_WORKDIR:-/tmp/mana-multinode-${USER}}"
TEST_HOME="$WORKDIR/home"
TIMEOUT_SECONDS="${MANA_TEST_TIMEOUT:-300}"
SSH_BIN="${MANA_TEST_SSH:-ssh}"
SCP_BIN="${MANA_TEST_SCP:-scp}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

APP="$WORKDIR/mpi_counter"
HOSTFILE="$WORKDIR/hosts.txt"
CKPTDIR="$WORKDIR/checkpoints"
TMPDIR="$WORKDIR/tmp"
LOGDIR="$WORKDIR/logs"
COORD_LOG="$WORKDIR/coordinator.log"
LAUNCH_LOG="$WORKDIR/launch.log"
RESTART_LOG="$WORKDIR/restart.log"
STATUS_FILE="$TEST_HOME/.mana.rc"

MPICC="$MANA_TEST_MPICH_HOME/bin/mpicc"
MPIEXEC="$MANA_TEST_MPICH_HOME/bin/mpiexec"
DMTCP_COORD="$MANA_HOME/bin/dmtcp_coordinator"
DMTCP_COMMAND="$MANA_HOME/bin/dmtcp_command"
MANA_LAUNCH="$MANA_HOME/bin/mana_launch"
MANA_RESTART="$MANA_HOME/bin/mana_restart"

for executable in \
    "$MPICC" "$MPIEXEC" "$DMTCP_COORD" "$DMTCP_COMMAND" \
    "$MANA_LAUNCH" "$MANA_RESTART" "$MANA_HOME/bin/lower-half"
do
    [[ -x "$executable" ]] || {
        echo "ERROR: missing executable: $executable" >&2
        exit 1
    }
done

[[ -r "$MANA_TEST_HOSTFILE" ]] || {
    echo "ERROR: cannot read hostfile: $MANA_TEST_HOSTFILE" >&2
    exit 1
}

mapfile -t NODES < <(
    sed \
      -e 's/[[:space:]]*#.*$//' \
      -e 's/\r$//' \
      -e 's/^[[:space:]]*//' \
      -e 's/[[:space:]]*$//' \
      "$MANA_TEST_HOSTFILE" |
    awk 'NF {host=$1; sub(/:.*/, "", host); if (!seen[host]++) print host}'
)

[[ "${#NODES[@]}" -gt 0 ]] || {
    echo "ERROR: no hosts found in $MANA_TEST_HOSTFILE" >&2
    exit 1
}

remote_target() {
    local host="$1"
    if [[ -n "$REMOTE_USER" ]]; then
        printf '%s@%s' "$REMOTE_USER" "$host"
    else
        printf '%s' "$host"
    fi
}

remote() {
    local host="$1"
    shift
    "$SSH_BIN" -n "$(remote_target "$host")" "$@"
}

copy_to() {
    local source="$1"
    local host="$2"
    local destination="$3"
    "$SCP_BIN" -q "$source" "$(remote_target "$host"):$destination" </dev/null
}

coord_host=""
coord_port=""

read_status_file() {
    coord_host="$(awk '/^Host:/ {print $2; exit}' "$STATUS_FILE")"
    coord_port="$(awk '/^Port:/ {print $2; exit}' "$STATUS_FILE")"
    [[ -n "$coord_host" && -n "$coord_port" ]] || {
        echo "ERROR: invalid coordinator status file: $STATUS_FILE" >&2
        return 1
    }
}

command_to_coordinator() {
    read_status_file
    "$DMTCP_COMMAND" \
      --coord-host "$coord_host" \
      --coord-port "$coord_port" \
      "$@"
}

cleanup() {
    set +e
    if [[ -s "$STATUS_FILE" ]]; then
        command_to_coordinator --kill >/dev/null 2>&1 || true
        command_to_coordinator --quit >/dev/null 2>&1 || true
    fi

    for host in "${NODES[@]}"; do
        remote "$host" "
          ps -eo pid=,args= |
          awk -v needle='$WORKDIR' 'index(\$0, needle) {print \$1}' |
          xargs -r kill -9 2>/dev/null || true
        " || true
    done

    ps -eo pid=,args= |
      awk -v needle="$WORKDIR" 'index($0, needle) {print $1}' |
      xargs -r kill -9 2>/dev/null || true
}
trap cleanup EXIT INT TERM

status_text() {
    command_to_coordinator --status 2>/dev/null || true
}

list_text() {
    command_to_coordinator --list 2>/dev/null || true
}

wait_for_peers() {
    local expected="$1"
    local limit="$2"
    local peers=0
    for ((second=0; second<limit; ++second)); do
        peers="$(status_text | awk -F= '/NUM_PEERS=/{gsub(/[[:space:]]/, "", $2); print $2; exit}')"
        peers="${peers:-0}"
        if [[ "$peers" -eq "$expected" ]]; then
            return 0
        fi
        sleep 1
    done
    echo "ERROR: expected $expected peers; observed $peers" >&2
    status_text >&2
    list_text >&2
    return 1
}

verify_images() {
    local images=0
    local temporary=0
    local host
    for host in "${NODES[@]}"; do
        count="$(remote "$host" "find '$CKPTDIR' -type f -name '*.dmtcp' -size +0c 2>/dev/null | wc -l")"
        tmp="$(remote "$host" "find '$CKPTDIR' -type f -name '*.tmp' 2>/dev/null | wc -l")"
        images=$((images + count))
        temporary=$((temporary + tmp))
    done
    [[ "$images" -ge "$RANKS" && "$temporary" -eq 0 ]]
}

wait_for_images() {
    local limit="$1"
    for ((second=0; second<limit; ++second)); do
        if verify_images; then
            return 0
        fi
        sleep 1
    done
    echo "ERROR: complete checkpoint images were not found" >&2
    return 1
}

mkdir -p "$WORKDIR" "$TEST_HOME" "$CKPTDIR" "$TMPDIR" "$LOGDIR"

# Preserve the rank layout supplied by the user, while stripping comments and CRLF.
sed \
  -e 's/[[:space:]]*#.*$//' \
  -e 's/\r$//' \
  -e 's/^[[:space:]]*//' \
  -e 's/[[:space:]]*$//' \
  -e '/^$/d' \
  "$MANA_TEST_HOSTFILE" > "$HOSTFILE"

for host in "${NODES[@]}"; do
    echo "Checking $host..."
    remote "$host" "
      set -eu
      test -x '$MANA_HOME/bin/mana_launch'
      test -x '$MANA_HOME/bin/mana_restart'
      test -x '$MANA_HOME/bin/lower-half'
      test -x '$MANA_TEST_MPICH_HOME/bin/hydra_pmi_proxy'
      mkdir -p '$WORKDIR' '$TEST_HOME' '$CKPTDIR' '$TMPDIR' '$LOGDIR'
      rm -f '$LOGDIR'/rank_*.log '$STATUS_FILE'
    "
    copy_to "$HOSTFILE" "$host" "$HOSTFILE"
done

"$MPICC" -O2 -g -Wall -Wextra -Wpedantic \
  "$SCRIPT_DIR/mpi_counter.c" -o "$APP"

for host in "${NODES[@]}"; do
    copy_to "$APP" "$host" "$APP"
    remote "$host" "chmod 755 '$APP'"
done

MPI_BASE=(
    "$MPIEXEC"
    -launcher ssh
    -launcher-exec "$SSH_BIN"
    -wdir "$WORKDIR"
    -f "$HOSTFILE"
    -n "$RANKS"
    -genv HOME "$TEST_HOME"
    -genv MANA_HOME "$MANA_HOME"
    -genv MPICH_HOME "$MANA_TEST_MPICH_HOME"
    -genv PATH "$MANA_HOME/bin:$MANA_TEST_MPICH_HOME/bin:$PATH"
    -genv LD_LIBRARY_PATH "$MANA_HOME/lib:$MANA_HOME/lib/dmtcp:$MANA_TEST_MPICH_HOME/lib:${LD_LIBRARY_PATH:-}"
    -genv HYDRA_LAUNCHER ssh
    -genv HYDRA_ENV all
)

echo "[1/7] Native MPICH/Hydra smoke"
"${MPI_BASE[@]}" "$APP" 5 "$LOGDIR"

echo "[2/7] Start persistent coordinator"
rm -f "$STATUS_FILE" "$COORD_LOG"
"$DMTCP_COORD" \
    --port "$COORD_PORT" \
    --interval 0 \
    --ckptdir "$CKPTDIR" \
    --coord-logfile "$COORD_LOG" \
    --daemon \
    --status-file "$STATUS_FILE"

for ((second=0; second<30; ++second)); do
    [[ -s "$STATUS_FILE" ]] && break
    sleep 1
done
[[ -s "$STATUS_FILE" ]] || {
    echo "ERROR: coordinator did not create $STATUS_FILE" >&2
    exit 1
}
read_status_file

for host in "${NODES[@]}"; do
    copy_to "$STATUS_FILE" "$host" "$STATUS_FILE"
    remote "$host" "
      host=\$(awk '/^Host:/ {print \$2; exit}' '$STATUS_FILE')
      port=\$(awk '/^Port:/ {print \$2; exit}' '$STATUS_FILE')
      '$DMTCP_COMMAND' --coord-host \"\$host\" --coord-port \"\$port\" --status >/dev/null
    "
done

echo "[3/7] Finite MANA smoke"
"${MPI_BASE[@]}" \
    "$MANA_LAUNCH" \
    --ckptdir "$CKPTDIR" \
    --tmpdir "$TMPDIR" \
    "$APP" 5 "$LOGDIR"

# The finite test disconnects, but the underlying coordinator remains persistent.
command_to_coordinator --status >/dev/null

# Start a fresh coordinator and clear finite-test logs/checkpoints.
command_to_coordinator --quit >/dev/null 2>&1 || true
rm -rf "$CKPTDIR" "$TMPDIR" "$LOGDIR"
mkdir -p "$CKPTDIR" "$TMPDIR" "$LOGDIR"
for host in "${NODES[@]}"; do
    remote "$host" "rm -rf '$CKPTDIR' '$TMPDIR' '$LOGDIR'; mkdir -p '$CKPTDIR' '$TMPDIR' '$LOGDIR'"
done
rm -f "$STATUS_FILE" "$COORD_LOG"
"$DMTCP_COORD" \
    --port "$COORD_PORT" \
    --interval 0 \
    --ckptdir "$CKPTDIR" \
    --coord-logfile "$COORD_LOG" \
    --daemon \
    --status-file "$STATUS_FILE"
for ((second=0; second<30; ++second)); do [[ -s "$STATUS_FILE" ]] && break; sleep 1; done
read_status_file
for host in "${NODES[@]}"; do copy_to "$STATUS_FILE" "$host" "$STATUS_FILE"; done

echo "[4/7] Long-running MANA launch"
nohup "${MPI_BASE[@]}" \
    "$MANA_LAUNCH" \
    --ckptdir "$CKPTDIR" \
    --tmpdir "$TMPDIR" \
    "$APP" 0 "$LOGDIR" \
    > "$LAUNCH_LOG" 2>&1 &
launch_pid="$!"
wait_for_peers "$RANKS" "$TIMEOUT_SECONDS"
sleep 10

echo "[5/7] Blocking checkpoint"
timeout --signal=TERM --kill-after=20s "$TIMEOUT_SECONDS" \
    "$DMTCP_COMMAND" \
    --coord-host "$coord_host" \
    --coord-port "$coord_port" \
    --bcheckpoint
wait_for_images "$TIMEOUT_SECONDS"

pre_max="$(awk 'match($0,/iteration=[0-9]+/){v=substr($0,RSTART+10,RLENGTH-10)+0;if(!s||v>m)m=v;s=1}END{if(s)print m}' "$LAUNCH_LOG")"
[[ -n "$pre_max" ]] || {
    echo "ERROR: no pre-checkpoint iteration found" >&2
    exit 1
}

echo "[6/7] Kill and restart"
command_to_coordinator --kill >/dev/null 2>&1 || true
sleep 3
kill "$launch_pid" >/dev/null 2>&1 || true
command_to_coordinator --quit >/dev/null 2>&1 || true

rm -f "$STATUS_FILE" "$COORD_LOG"
"$DMTCP_COORD" \
    --port "$COORD_PORT" \
    --interval 0 \
    --ckptdir "$CKPTDIR" \
    --coord-logfile "$COORD_LOG" \
    --daemon \
    --status-file "$STATUS_FILE"
for ((second=0; second<30; ++second)); do [[ -s "$STATUS_FILE" ]] && break; sleep 1; done
read_status_file
for host in "${NODES[@]}"; do copy_to "$STATUS_FILE" "$host" "$STATUS_FILE"; done

MPI_RESTART=(
    "$MPIEXEC"
    -launcher ssh
    -launcher-exec "$SSH_BIN"
    -wdir "$CKPTDIR"
    -f "$HOSTFILE"
    -n "$RANKS"
    -genv HOME "$TEST_HOME"
    -genv MANA_HOME "$MANA_HOME"
    -genv MPICH_HOME "$MANA_TEST_MPICH_HOME"
    -genv PATH "$MANA_HOME/bin:$MANA_TEST_MPICH_HOME/bin:$PATH"
    -genv LD_LIBRARY_PATH "$MANA_HOME/lib:$MANA_HOME/lib/dmtcp:$MANA_TEST_MPICH_HOME/lib:${LD_LIBRARY_PATH:-}"
    -genv DMTCP_LOG_LEVEL trace
    -genv HYDRA_LAUNCHER ssh
    -genv HYDRA_ENV all
)

nohup "${MPI_RESTART[@]}" \
    "$MANA_RESTART" \
    --verbose \
    --ckptdir "$CKPTDIR" \
    --tmpdir "$TMPDIR" \
    --restartdir "$CKPTDIR" \
    > "$RESTART_LOG" 2>&1 &
restart_pid="$!"
wait_for_peers "$RANKS" "$TIMEOUT_SECONDS"
sleep 15

kill "$restart_pid" >/dev/null 2>&1 || true

echo "[7/7] Verify rollback, collective result, and progress"
python3 - "$LAUNCH_LOG" "$RESTART_LOG" "$RANKS" <<'EOF_VERIFY_PY'
import re
import sys
from pathlib import Path

launch_path, restart_path, ranks_text = sys.argv[1:]
ranks = int(ranks_text)
pattern = re.compile(
    r"rank=(?P<rank>\d+)/(?P<size>\d+) .*?iteration=(?P<iteration>\d+) "
    r"allreduce_sum=(?P<sum>-?\d+)"
)

def parse(path: str):
    records = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.search(line)
        if match:
            records.append({key: int(value) for key, value in match.groupdict().items()})
    return records

before = parse(launch_path)
after = parse(restart_path)
if not before:
    raise SystemExit("No pre-checkpoint MPI records found")
if not after:
    raise SystemExit("No post-restart MPI records found")

before_max = max(record["iteration"] for record in before)
after_first = min(record["iteration"] for record in after)
after_last = max(record["iteration"] for record in after)

if after_first > before_max:
    raise SystemExit(
        f"Rollback not demonstrated: first restart iteration {after_first} "
        f"> pre-checkpoint maximum {before_max}"
    )
if after_last <= after_first:
    raise SystemExit("No forward progress after restart")

rank_sum = ranks * (ranks - 1) // 2
for record in after:
    expected = ranks * (2 * record["iteration"]) + rank_sum
    if record["size"] != ranks:
        raise SystemExit(f"Unexpected communicator size: {record}")
    if record["sum"] != expected:
        raise SystemExit(
            f"Incorrect Allreduce at iteration {record['iteration']}: "
            f"expected {expected}, observed {record['sum']}"
        )

print(f"Pre-checkpoint maximum iteration: {before_max}")
print(f"First post-restart iteration:      {after_first}")
print(f"Last post-restart iteration:       {after_last}")
print(f"Demonstrated rewind:               {before_max - after_first}")
print("Post-restart MPI_Allreduce:        correct")
EOF_VERIFY_PY

echo "PASS: multi-node MPICH/Hydra checkpoint-restart validation"
echo "Evidence directory: $WORKDIR"
