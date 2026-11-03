#!/usr/bin/env python3
"""Summarize the traces that cudatrace and mpitrace wrote to a directory.

    analyze.py DIR [--gran 2M] [--small-limit 1M] [--ipc-threshold 466019]
               [--per-proc]

Reports, over all traced processes:
  * which allocation APIs are used, and how often;
  * the sizes of device allocations, and what one VMM handle per allocation
    (rounded up to --gran) would cost at the peak;
  * when allocations happen (startup only, or throughout the run) and how
    long they live;
  * which libraries make them;
  * which allocations IPC exports, and which memory MPI calls send from.
"""
import argparse
import bisect
import collections
import csv
import glob
import os
import re
import statistics

DEVICE = {"cuMemAlloc_v2": "device", "cuMemAllocPitch_v2": "device",
          "cuMemAllocManaged": "managed",
          "cuMemAllocAsync": "async", "cuMemAllocAsync_ptsz": "async",
          "cuMemAllocFromPoolAsync": "async",
          "cuMemAllocFromPoolAsync_ptsz": "async",
          "cuMemAllocHost_v2": "pinned", "cuMemHostAlloc": "pinned",
          "cuMemHostRegister_v2": "registered",
          "cuMemMap": "vmm"}
FREE = {"cuMemFree_v2", "cuMemFreeAsync", "cuMemFreeAsync_ptsz",
        "cuMemFreeHost", "cuMemHostUnregister", "cuMemUnmap"}
IPC = {"cuIpcGetMemHandle", "cuMemExportToShareableHandle"}
BUCKETS = [4 << 10, 64 << 10, 512 << 10, 1 << 20, 2 << 20, 4 << 20,
           64 << 20, 1 << 30]


def size_arg(s):
    m = re.fullmatch(r"(\d+)([KMG]?)", s.upper())
    if not m:
        raise argparse.ArgumentTypeError(f"bad size {s}")
    return int(m[1]) << {"": 0, "K": 10, "M": 20, "G": 30}[m[2]]


def human(n):
    for unit in ("B", "K", "M", "G"):
        if abs(n) < 1024 or unit == "G":
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024


def bucket_name(i):
    lo = human(BUCKETS[i - 1]) if i else "0"
    hi = human(BUCKETS[i]) if i < len(BUCKETS) else "inf"
    return f"[{lo}, {hi})"


def histogram(sizes):
    h = collections.Counter(bisect.bisect_right(BUCKETS, s) for s in sizes)
    return [(bucket_name(i), h[i]) for i in range(len(BUCKETS) + 1) if h[i]]


def roundup(n, g):
    return (n + g - 1) // g * g


class Alloc:
    def __init__(self, t, api, ptr, size, caller):
        self.t, self.api, self.ptr, self.size = t, api, ptr, size
        self.kind, self.caller, self.freed = DEVICE[api], caller, None
        self.mpi_msgs = 0


class Proc:
    """One process: its allocations, replayed in time order with its MPI calls."""

    def __init__(self, pid, cuda_rows, mpi_rows, args):
        self.pid, self.args = pid, args
        self.rank = int(mpi_rows[0]["rank"]) if mpi_rows else None
        self.api_count = collections.Counter(r["api"] for r in cuda_rows)
        self.allocs, self.ipc, self.mpi = [], [], []
        self.peak_req = self.peak_round = 0
        events = [(int(r["t_ns"]), 0, r) for r in cuda_rows] + \
                 [(int(r["t_ns"]), 1, r) for r in mpi_rows]
        events.sort(key=lambda e: (e[0], e[1]))
        self.t0 = events[0][0] if events else 0
        self.t1 = events[-1][0] if events else 0
        self.replay(events)

    def replay(self, events):
        starts, live = [], {}  # sorted start addresses; start -> Alloc
        req = rnd = 0

        def find(addr):
            i = bisect.bisect_right(starts, addr) - 1
            if i >= 0:
                a = live[starts[i]]
                if addr < a.ptr + max(a.size, 1):
                    return a
            return None

        for t, is_mpi, r in events:
            if is_mpi:
                for key in ("sendbuf", "recvbuf"):
                    addr = int(r[key], 16)
                    if addr:
                        a = find(addr)
                        if a:
                            a.mpi_msgs += 1
                        self.mpi.append((r["api"], int(r["bytes"]), a))
                continue
            if r["status"] != "0":
                continue
            api, ptr, size = r["api"], int(r["ptr"], 16), int(r["size"])
            if api in DEVICE:
                if ptr in live:  # e.g. a VMM range mapped piece by piece
                    continue
                a = Alloc(t, api, ptr, size, r["caller"])
                self.allocs.append(a)
                bisect.insort(starts, ptr)
                live[ptr] = a
                if a.kind == "device":
                    req += size
                    rnd += roundup(size, self.args.gran)
                    self.peak_req = max(self.peak_req, req)
                    self.peak_round = max(self.peak_round, rnd)
            elif api in FREE:
                a = live.pop(ptr, None)
                if a:
                    starts.remove(ptr)
                    a.freed = t
                    if a.kind == "device":
                        req -= a.size
                        rnd -= roundup(a.size, self.args.gran)
            elif api in IPC and api == "cuIpcGetMemHandle":
                self.ipc.append((api, find(ptr)))
            elif api in IPC:
                self.ipc.append((api, None))  # a VMM handle, not an address


def load(directory):
    procs = collections.defaultdict(lambda: ([], []))
    for path in glob.glob(os.path.join(directory, "*.csv")):
        m = re.fullmatch(r"(cuda|mpi)-.*-(\d+)\.csv", os.path.basename(path))
        if not m:
            continue
        with open(path, newline="") as f:
            rows = list(csv.DictReader(f))
        procs[int(m[2])][0 if m[1] == "cuda" else 1].extend(rows)
    return procs


def section(title):
    print(f"\n== {title}")


def table(rows, indent="  "):
    rows = [tuple(str(c) for c in r) for r in rows]
    if not rows:
        print(indent + "(none)")
        return
    w = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    for r in rows:
        print(indent + "  ".join(c.rjust(w[i]) if i else c.ljust(w[i])
                                 for i, c in enumerate(r)))


def report(procs, args):
    allocs = [a for p in procs for a in p.allocs]
    dev = [a for a in allocs if a.kind == "device"]
    g = args.gran

    section("processes")
    table([("pid", "rank", "allocs", "device", "peak requested",
            f"peak at {human(g)} rounding", "run time")] +
          [(p.pid, "-" if p.rank is None else p.rank, len(p.allocs),
            sum(a.kind == "device" for a in p.allocs), human(p.peak_req),
            human(p.peak_round), f"{(p.t1 - p.t0) / 1e9:.1f}s")
           for p in procs])

    section("API calls (successful or not)")
    total = collections.Counter()
    for p in procs:
        total.update(p.api_count)
    table(sorted(total.items(), key=lambda kv: -kv[1]))

    section("allocations by kind")
    kinds = collections.defaultdict(list)
    for a in allocs:
        kinds[a.kind].append(a.size)
    table([("kind", "count", "bytes", "median size")] +
          [(k, len(v), human(sum(v)), human(statistics.median(v)))
           for k, v in sorted(kinds.items())])

    section("device allocation sizes (cuMemAlloc / cudaMalloc)")
    table([("size", "count")] + histogram([a.size for a in dev]))
    if dev:
        mult = sum(a.size % g == 0 for a in dev)
        small = sum(a.size < args.small_limit for a in dev)
        print(f"  multiples of {human(g)}: {mult}/{len(dev)};"
              f"  below {human(args.small_limit)}: {small}/{len(dev)}")
        req = sum(p.peak_req for p in procs)
        rnd = sum(p.peak_round for p in procs)
        print(f"  peak live, summed over processes: requested {human(req)},"
              f" one handle each {human(rnd)}"
              f" (+{100 * (rnd - req) / max(req, 1):.1f}%)")
    for k in ("async", "managed", "pinned"):
        if kinds.get(k):
            section(f"{k} allocation sizes")
            table([("size", "count")] + histogram(kinds[k]))

    section("when device allocations happen (tenths of each process's run)")
    bins = [0] * 10
    for p in procs:
        span = max(p.t1 - p.t0, 1)
        for a in p.allocs:
            if a.kind == "device":
                bins[min(9, (a.t - p.t0) * 10 // span)] += 1
    print("  " + " ".join(f"{b:6d}" for b in bins))
    freed = [a for a in dev if a.freed]
    print(f"  freed before exit: {len(freed)}/{len(dev)}")
    if freed:
        life = sorted((a.freed - a.t) / 1e6 for a in freed)
        print(f"  lifetime of freed ones: median {statistics.median(life):.2f}"
              f" ms, max {life[-1]:.2f} ms")

    section("who allocates device memory")
    by = collections.defaultdict(lambda: [0, 0])
    for a in dev:
        by[a.caller][0] += 1
        by[a.caller][1] += a.size
    table([("caller", "count", "bytes")] +
          [(c, n, human(b)) for c, (n, b) in
           sorted(by.items(), key=lambda kv: -kv[1][1])])

    section("IPC exports (what other processes get to map)")
    ipc = collections.Counter(
        (api, a.kind if a else "?", human(a.size) if a else "?")
        for p in procs for api, a in p.ipc)
    table([("api", "kind", "allocation size", "count")] +
          [k + (n,) for k, n in sorted(ipc.items())])
    opens = sum(p.api_count["cuIpcOpenMemHandle_v2"] for p in procs)
    print(f"  handles opened by peers (cuIpcOpenMemHandle): {opens}")

    msgs = [m for p in procs for m in p.mpi]
    if not msgs:
        return
    section("MPI buffers by memory kind")
    where = collections.defaultdict(lambda: [0, 0])
    for api, n, a in msgs:
        k = a.kind if a else "host (unregistered)"
        where[k][0] += 1
        where[k][1] += n
    table([("memory", "buffers", "bytes")] +
          [(k, c, human(b)) for k, (c, b) in sorted(where.items())])

    on_dev = [(n, a) for _, n, a in msgs if a and a.kind != "pinned"
              and a.kind != "registered"]
    section("MPI buffer sizes on GPU memory (send and receive buffers)")
    table([("size", "count")] + histogram([n for n, _ in on_dev]))
    big = [(n, a) for n, a in on_dev if n >= args.ipc_threshold]
    exposed = [(n, a) for n, a in big if a.size < args.small_limit]
    print(f"  buffers >= {human(args.ipc_threshold)} (likely cuda_ipc):"
          f" {len(big)}; of those, in allocations below"
          f" {human(args.small_limit)} (would be pooled): {len(exposed)}")
    used = {id(a): a for _, a in on_dev}.values()
    print(f"  distinct GPU allocations used by MPI: {len(used)}, sizes:")
    table([("size", "count")] + histogram([a.size for a in used]), "    ")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--gran", type=size_arg, default=2 << 20,
                    help="VMM granularity (default 2M)")
    ap.add_argument("--small-limit", type=size_arg, default=1 << 20,
                    help="pool allocations below this (default 1M)")
    ap.add_argument("--ipc-threshold", type=size_arg, default=466019,
                    help="message size from which UCX uses cuda_ipc")
    ap.add_argument("--per-proc", action="store_true",
                    help="also print a full report for each process")
    args = ap.parse_args()

    raw = load(args.dir)
    if not raw:
        raise SystemExit(f"no cuda-*.csv or mpi-*.csv in {args.dir}")
    procs = [Proc(pid, c, m, args) for pid, (c, m) in sorted(raw.items())]
    print(f"### all {len(procs)} processes")
    report(procs, args)
    if args.per_proc:
        for p in procs:
            print(f"\n### pid {p.pid}" +
                  ("" if p.rank is None else f" (rank {p.rank})"))
            report([p], args)


if __name__ == "__main__":
    main()
