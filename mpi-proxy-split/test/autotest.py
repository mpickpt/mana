#!/usr/bin/env python3
"""End-to-end tests of MANA, modeled on DMTCP's test/autotest.py.

Each test launches an MPI program under MANA, then repeats: checkpoint, kill
and restart.  After each step, every rank must report progress.  At the end,
the harness tells the program at which iteration to stop, and the program
must exit with status 0 and print that it passed.  The programs in this
directory call MPI_Abort() on any wrong value, so a test fails if a rank dies
or stops, a step times out, or a program reports an error.  Tests of kind
"run" instead run to completion on their own and must print an expected line.

Usage: autotest.py [options] [TEST ...]   (see --help)
"""

import argparse
import glob
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
MANA_ROOT = os.path.abspath(os.path.join(TEST_DIR, "..", ".."))


# In the arguments of a test, stands for the MPI library by its full path.
MPI_LIBRARY = "{mpi_library}"


class Test:
  """A test program and how to run it.

  kind "loop": runs until told to stop (see mana_test.h); checkpointed and
               restarted --cycles times, then stopped.
  kind "run":  runs to completion and must print 'expect'; with 'restart',
               it is checkpointed, killed and restarted once on the way.
  fails:       the run must end with a nonzero status (e.g. MPI_Abort).
  known_bug:   a bug that this test still hits; its failure is reported but
               does not fail the run.
  """

  def __init__(self, name, ranks, args=(), kind="loop", expect=None,
               restart=False, fails=False, known_bug=None, native_args=None,
               program=None):
    self.name = name
    self.ranks = ranks
    self.args = list(args)
    self.kind = kind
    self.expect = expect
    self.restart = restart
    self.fails = fails
    self.known_bug = known_bug
    # The program, relative to this directory.
    self.program = program or name
    # Arguments for a native run of kind "run".
    self.native_args = native_args if native_args is not None else self.args


TESTS = [
  Test("p2p_blocking", 3),
  Test("p2p_nonblocking", 4),
  Test("p2p_ring", 4),
  Test("p2p_any_source", 4),
  Test("p2p_probe", 4,
       known_bug="MPI_Probe and MPI_Iprobe do not see the messages that a "
                 "checkpoint drained"),
  Test("p2p_large", 2),
  Test("p2p_proc_null", 4),
  Test("fortran_p2p", 2),
  Test("collectives", 4),
  Test("nonblocking_collectives", 4),
  Test("communicators", 4),
  Test("cartesian", 4,
       known_bug="restart rebuilds a Cartesian communicator without its "
                 "topology"),
  Test("datatypes", 2),
  Test("memory", 2),
  Test("init_thread", 2, kind="run", expect="init_thread: PASS"),
  Test("exit_after_finalize", 4, args=["8"], kind="run", restart=True,
       expect="exit_after_finalize: PASS", native_args=["1"]),
  Test("attributes", 2),
  Test("misc_calls", 2),
  Test("fortran_nonblocking", 2),
  Test("finalize_unsync", 4, args=["8"], kind="run", restart=True,
       native_args=["1"]),
  Test("abort", 2, kind="run", fails=True, expect="abort: calling MPI_Abort"),
  Test("dlopen_mpi", 2, args=[MPI_LIBRARY, "8"], kind="run", restart=True,
       native_args=[MPI_LIBRARY, "1"]),
  # The example that the documentation uses, as its users run it.
  Test("ring_example", 4, args=["-n", "20", "-s", "0.25"], kind="run",
       restart=True, expect="ring: done", program="../examples/ring"),
]


class Failure(Exception):
  pass


# The progress report of mt_continue() in mana_test.h.
PROGRESS = re.compile(rb"^(\S+): rank (\d+): iteration (\d+)$", re.M)


def free_port():
  s = socket.socket()
  s.bind(("", 0))
  port = s.getsockname()[1]
  s.close()
  return port


def shm_files():
  return set(os.listdir("/dev/shm")) if os.path.isdir("/dev/shm") else set()


def remove_unused_shm(before):
  """Removes /dev/shm files created since 'before' that no process maps
  (an MPI job that was killed leaves its segments behind).  It first waits
  until each file is 1 s old: a job that just made one may not map it yet."""
  new = {}
  for name in shm_files() - before:
    try:
      new[name] = os.path.getmtime(os.path.join("/dev/shm", name))
    except OSError:
      pass
  if not new:
    return
  time.sleep(max(0, max(new.values()) + 1 - time.time()))
  mapped = set()
  for maps in glob.glob("/proc/[0-9]*/maps"):
    try:
      with open(maps) as f:
        for line in f:
          if "/dev/shm/" in line:
            mapped.add(line.split("/dev/shm/")[1].split()[0])
    except OSError:
      pass
  for name in set(new) - mapped:
    try:
      os.unlink(os.path.join("/dev/shm", name))
    except OSError:
      pass


class Run:
  """One test run: its directory, coordinator port and processes."""

  def __init__(self, test, opts):
    self.test = test
    self.opts = opts
    self.bin = os.path.join(opts.mana_root, "bin")
    self.dir = tempfile.mkdtemp(prefix=test.name + ".", dir=opts.workdir)
    home = os.path.join(self.dir, "home")
    os.mkdir(home)
    self.port = free_port()
    self.env = dict(os.environ)
    # A private HOME keeps ~/.mana.rc of this run away from the user's.
    self.env.update({
      "HOME": home,
      "MT_CONTROL": self.dir,
      "DMTCP_COORD_PORT": str(self.port),
      # DMTCP writes its error messages here when stderr is not a file.
      "JALIB_STDERR_PATH": os.path.join(self.dir, "dmtcp-errors.log"),
    })
    self.proc = None
    self.logs = []

  def timeout(self, seconds):
    return seconds * self.opts.slow

  def launch(self, cmd, log):
    cmd = [mpi_library() if arg == MPI_LIBRARY else arg for arg in cmd]
    path = os.path.join(self.dir, log)
    self.logs.append(path)
    out = open(path, "w")
    launcher = self.opts.launcher.format(n=self.test.ranks).split()
    self.proc = subprocess.Popen(launcher + cmd, cwd=self.dir, env=self.env,
                                 stdout=out, stderr=subprocess.STDOUT,
                                 start_new_session=True)
    out.close()

  def command(self, *args):
    return subprocess.run([os.path.join(self.bin, "dmtcp_command"),
                           "-p", str(self.port)] + list(args),
                          env=self.env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, universal_newlines=True,
                          timeout=self.timeout(60))

  def status(self):
    """Returns (number of ranks, running) as the coordinator sees them."""
    out = self.command("-s").stdout
    peers = re.search(r"NUM_PEERS=(\d+)", out)
    running = re.search(r"RUNNING=(\w+)", out)
    if not peers or not running:
      return (0, False)
    return (int(peers.group(1)), running.group(1) == "yes")

  def start_coordinator(self):
    for _ in range(50):
      rc = subprocess.run([os.path.join(self.bin, "mana_coordinator"),
                           "-p", str(self.port)], env=self.env,
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode
      if rc == 0:
        return
      time.sleep(0.2)  # The previous coordinator may still hold the port.
    raise Failure("cannot start the coordinator")

  def check_alive(self):
    if self.proc is not None and self.proc.poll() is not None:
      raise Failure("the job exited (status %d)" % self.proc.returncode)

  def wait_for(self, cond, what, seconds=60):
    end = time.time() + self.timeout(seconds)
    while time.time() < end:
      self.check_alive()
      if cond():
        return
      time.sleep(0.1)
    raise Failure("timed out waiting for " + what)

  def wait_running(self, what):
    """For a program without progress reports: every rank must run for 1 s."""
    n = self.test.ranks
    self.wait_for(lambda: self.status() == (n, True), what)
    time.sleep(self.timeout(1))
    self.check_alive()
    if self.status() != (n, True):
      raise Failure("ranks died " + what)

  def log_size(self):
    return os.path.getsize(self.logs[-1])

  def progress(self, start=0):
    """Returns {rank: [iteration, ...]} as reported in the current log after
    byte 'start'."""
    with open(self.logs[-1], "rb") as f:
      f.seek(start)
      text = f.read()
    seen = {}
    for m in PROGRESS.finditer(text):
      if m.group(1).decode(errors="replace") == self.test.name:
        seen.setdefault(int(m.group(2)), []).append(int(m.group(3)))
    return seen

  def wait_progress(self, what, start=0):
    """Waits until every rank reports a later iteration than the first one
    it reports after byte 'start' of the current log."""
    end = time.time() + self.timeout(60)
    while True:
      self.check_alive()
      seen = self.progress(start)
      stalled = [str(r) for r in range(self.test.ranks)
                 if len(seen.get(r, [])) < 2 or seen[r][-1] <= seen[r][0]]
      if not stalled:
        return
      if time.time() > end:
        raise Failure("timed out waiting for progress %s (ranks without "
                      "progress: %s)" % (what, " ".join(stalled)))
      time.sleep(0.1)

  def finish(self):
    """Tells the ranks at which iteration to stop; the program must then
    exit with status 0 and print that it passed that many iterations."""
    seen = self.progress()
    latest = max(its[-1] for its in seen.values())
    # Reports are 0.1 s or more apart, so this is 1 s or more ahead.
    step = max(b - a for its in seen.values() for a, b in zip(its, its[1:]))
    stop = latest + 10 * step
    tmp = os.path.join(self.dir, "stop.tmp")
    with open(tmp, "w") as f:
      f.write("%d\n" % stop)
    os.rename(tmp, os.path.join(self.dir, "stop"))  # No rank reads half.
    self.wait_end()
    self.check_status()
    self.check_output("%s: PASS (%d iterations)" % (self.test.name, stop))

  def wait_end(self):
    try:
      self.proc.wait(timeout=self.timeout(120))
    except subprocess.TimeoutExpired:
      raise Failure("timed out waiting for the job to end")

  def checkpoint(self):
    mark = time.time() - 0.01
    self.command("-bc")  # May return before the images are written.

    def images():
      files = glob.glob(os.path.join(self.dir, "ckpt_rank_*", "ckpt_*.dmtcp"))
      return len([f for f in files if os.path.getmtime(f) >= mark])

    self.wait_for(lambda: images() >= self.test.ranks, "the checkpoint")

  def kill(self):
    """Kills the job: through the coordinator, then any process left."""
    if self.proc is not None and self.proc.poll() is None:
      self.command("-k")
      end = time.time() + self.timeout(20)
      while self.proc.poll() is None and time.time() < end:
        time.sleep(0.1)
      if self.proc.poll() is None:
        try:
          os.killpg(self.proc.pid, signal.SIGKILL)
        except OSError:
          pass
        self.proc.wait()
    # A rank can outlive mpirun; this run's processes all have its HOME.
    tag = ("HOME=" + self.env["HOME"]).encode() + b"\0"
    for environ in glob.glob("/proc/[0-9]*/environ"):
      try:
        with open(environ, "rb") as f:
          if tag in f.read():
            os.kill(int(environ.split("/")[2]), signal.SIGKILL)
      except (OSError, ValueError):
        pass

  def stop(self):
    self.kill()
    self.command("-q")  # Stop this run's coordinator, if any is left.

  def restart(self, log):
    self.kill()
    self.start_coordinator()
    self.launch([os.path.join(self.bin, "mana_restart")], log)

  def run_loop(self, report):
    t = self.test
    self.start_coordinator()
    self.launch([os.path.join(self.bin, "mana_launch"), self.exe()] + t.args,
                "launch.out")
    self.wait_progress("after launch")
    for cycle in range(self.opts.cycles):
      self.checkpoint()
      self.wait_progress("after the checkpoint", self.log_size())
      report("ckpt:PASSED")
      self.restart("restart-%d.out" % (cycle + 1))
      self.wait_progress("after the restart")
      report("rstr:PASSED")
    self.finish()
    report("end:PASSED")

  def run_to_end(self, report):
    t = self.test
    self.start_coordinator()
    self.launch([os.path.join(self.bin, "mana_launch"), self.exe()] + t.args,
                "launch.out")
    if t.restart:
      self.wait_running("after launch")
      self.checkpoint()
      report("ckpt:PASSED")
      self.restart("restart-1.out")
    self.wait_end()
    self.check_status()
    self.check_output()
    report("run:PASSED")

  def run_native(self, report):
    t = self.test
    if t.kind == "loop":
      self.launch([self.exe()] + t.args, "native.out")
      self.wait_progress("after the start")
      self.finish()
    else:
      self.launch([self.exe()] + t.native_args, "native.out")
      self.wait_end()
      self.check_status()
      self.check_output()
    report("native:PASSED")

  def check_status(self):
    rc = self.proc.returncode
    if self.test.fails and rc == 0:
      raise Failure("the job exited with status 0, but must fail")
    if not self.test.fails and rc != 0:
      raise Failure("the job exited with status %d" % rc)

  def check_output(self, expect=None):
    expect = expect or self.test.expect or (self.test.name + ": PASS")
    with open(self.logs[-1], errors="replace") as f:
      if expect not in f.read():
        raise Failure("'%s' not in %s" % (expect, os.path.basename(
            self.logs[-1])))

  def exe(self):
    path = os.path.normpath(os.path.join(TEST_DIR, self.test.program))
    if not os.path.exists(path):
      raise Failure("%s not built (run make in %s)" % (path, TEST_DIR))
    return path

  def log_tail(self, lines=15):
    text = []
    errors = os.path.join(self.dir, "dmtcp-errors.log")
    for path in self.logs + [errors]:
      if os.path.exists(path):
        with open(path, errors="replace") as f:
          tail = f.read().splitlines()[-lines:]
        if tail:
          text.append("    --- %s" % os.path.basename(path))
          text += ["    " + line for line in tail]
    return "\n".join(text)


def mpi_library():
  """The MPI library that the tests are linked with, by its full path."""
  out = subprocess.run(["ldd", os.path.join(TEST_DIR, "p2p_ring")],
                       stdout=subprocess.PIPE, universal_newlines=True).stdout
  m = re.search(r"=> (/\S*/libmpi\w*\.so\S*) ", out)
  if m is None:
    raise Failure("ldd finds no MPI library in p2p_ring")
  return m.group(1)


def run_test(test, opts):
  """Runs one test; returns (passed, text of the result)."""
  steps = []
  shm_before = shm_files()
  run = Run(test, opts)
  try:
    if opts.native:
      run.run_native(steps.append)
    elif test.kind == "run":
      run.run_to_end(steps.append)
    else:
      run.run_loop(steps.append)
    passed, detail = True, ""
  except (Failure, subprocess.TimeoutExpired) as e:
    passed = False
    detail = "FAILED: %s\n    (logs in %s)\n%s" % (e, run.dir, run.log_tail())
  finally:
    run.stop()
    remove_unused_shm(shm_before)
  if passed and not opts.keep:
    shutil.rmtree(run.dir, ignore_errors=True)
  return passed, " ".join(steps), detail


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
  parser.add_argument("tests", nargs="*", help="tests to run (default: all)")
  parser.add_argument("--list", action="store_true", help="list the tests")
  parser.add_argument("--cycles", type=int, default=2,
                      help="checkpoint-restart cycles per test (default 2)")
  parser.add_argument("--slow", type=float, default=1,
                      help="multiply every timeout and wait by this factor")
  parser.add_argument("--native", action="store_true",
                      help="run the test programs without MANA, to check "
                           "the tests themselves")
  parser.add_argument("--retry-once", action="store_true",
                      help="run a failed test a second time")
  parser.add_argument("--keep", action="store_true",
                      help="keep the directories of passed tests too")
  parser.add_argument("--launcher", default="mpirun -np {n}",
                      help="MPI launcher; {n} is the number of ranks "
                           "(default 'mpirun -np {n}', e.g. 'srun -n {n}')")
  parser.add_argument("--mana-root", default=MANA_ROOT,
                      help="MANA installation to test (default %(default)s)")
  parser.add_argument("--workdir", default=None,
                      help="where to put the test runs (default: $TMPDIR)")
  opts = parser.parse_args()

  if opts.list:
    for t in TESTS:
      bug = "  [known bug: %s]" % t.known_bug if t.known_bug else ""
      print("%-26s %d ranks, %s%s" % (t.name, t.ranks, t.kind, bug))
    return 0
  unknown = set(opts.tests) - {t.name for t in TESTS}
  if unknown:
    parser.error("unknown tests: " + ", ".join(sorted(unknown)))
  tests = [t for t in TESTS if not opts.tests or t.name in opts.tests]
  opts.workdir = opts.workdir or tempfile.gettempdir()
  os.makedirs(opts.workdir, exist_ok=True)

  failed, known, xpass = [], [], []
  suite_start = time.time()
  for test in tests:
    sys.stdout.write("%-26s " % test.name)
    sys.stdout.flush()
    start = time.time()
    passed, steps, detail = run_test(test, opts)
    if not passed and opts.retry_once and not test.known_bug:
      first = detail.splitlines()[0]
      passed, steps, detail = run_test(test, opts)
      steps += " (first run %s; passed on retry)" % first if passed else ""
    steps += " (%.1f s)" % (time.time() - start)
    if passed:
      if test.known_bug and not opts.native:
        xpass.append(test.name)
        steps += "  (known bug did not show: %s)" % test.known_bug
      print(steps)
    elif test.known_bug and not opts.native:
      known.append(test.name)
      # The first line of the failure, to tell the known bug from a new one.
      print("%s KNOWN BUG: %s\n    (%s)" % (steps, test.known_bug,
                                          detail.splitlines()[0]))
      if opts.keep:
        print(detail)
    else:
      failed.append(test.name)
      print("%s %s" % (steps, detail))
    sys.stdout.flush()

  total = len(tests)
  print("== Tests passed: %d of %d%s%s, in %.0f s ==" % (
      total - len(failed) - len(known), total,
      ", known bugs: %d" % len(known) if known else "",
      ", failed: %s" % " ".join(failed) if failed else "",
      time.time() - suite_start))
  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
