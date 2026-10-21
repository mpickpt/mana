#!/usr/bin/env python3
"""Test the restart wrapper without MPI or a running coordinator."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = Path(os.environ.get(
  "MANA_RESTART_WRAPPER", ROOT / "bin" / "mana_restart"))


class ManaRestartTest(unittest.TestCase):
  def setUp(self):
    self.temp = tempfile.TemporaryDirectory(prefix="mana-restart-test-")
    self.addCleanup(self.temp.cleanup)
    self.root = Path(self.temp.name)
    self.fake_bin = self.root / "fake-mana" / "bin"
    self.fake_bin.mkdir(parents=True)
    self.home = self.root / "home"
    self.home.mkdir()
    self.cwd = self.root / "unrelated"
    self.cwd.mkdir()
    self.restart = self.home / "restart"
    for rank in range(2):
      rank_dir = self.restart / ("ckpt_rank_" + str(rank))
      rank_dir.mkdir(parents=True)
      (rank_dir / ("ckpt_" + str(rank) + ".dmtcp")).touch()
      (rank_dir / "header.mana").write_text(
        "p2p_wait=blocking\nfds=" + str(10 + rank) + "\n")
    (self.restart / "README").write_text("Ignore regular root files.\n")
    self.capture = self.root / "args.txt"
    self.env_capture = self.root / "env.txt"
    shutil.copy2(WRAPPER, self.fake_bin / "mana_restart")
    os.chmod(self.fake_bin / "mana_restart", 0o755)
    self._exe(self.fake_bin / "dmtcp_command", "#!/bin/sh\nexit 0\n")
    self._exe(self.fake_bin / "lower-half", (
      '#!/bin/sh\n'
      'printf "%s\\n" "$@" > "$MANA_TEST_CAPTURE"\n'
      'printf "%s\\n" "$MANA_P2P_WAIT" "$MANA_RESERVED_FDS" '
      '> "$MANA_TEST_ENV_CAPTURE"\n'))
    (self.home / ".mana.rc").write_text("Host: localhost\nPort: 7780\n")

  @staticmethod
  def _exe(path, content):
    path.write_text(content)
    os.chmod(path, 0o755)

  def run_wrapper(self, *args, rank=None):
    env = os.environ.copy()
    for var in ("SLURM_JOB_ID", "SLURM_PROCID", "PMI_RANK", "PMIX_RANK",
                "MANA_P2P_WAIT", "MANA_RESERVED_FDS"):
      env.pop(var, None)
    env.update(HOME=str(self.home), MANA_TEST_CAPTURE=str(self.capture),
               MANA_TEST_ENV_CAPTURE=str(self.env_capture))
    if rank is not None:
      env["PMI_RANK"] = str(rank)
    return subprocess.run(
      [str(self.fake_bin / "mana_restart"), *args], cwd=self.cwd,
      env=env, text=True, capture_output=True, check=False, timeout=10)

  def assert_success(self, result):
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

  def captured(self):
    return self.capture.read_text().splitlines()

  def test_outside_current_directory(self):
    result = self.run_wrapper("--verbose", "--restartdir", str(self.restart))
    self.assert_success(result)
    args = self.captured()
    self.assertEqual(args[-2:], ["--restartdir", str(self.restart)])
    self.assertEqual(args.count("--restartdir"), 1)

  def test_temp_detection(self):
    (self.restart / "ckpt_rank_1" / "ckpt_1.dmtcp.temp").touch()
    result = self.run_wrapper("--restartdir", str(self.restart))
    self.assertNotEqual(result.returncode, 0)
    self.assertIn("Restart directory has .temp files", result.stdout)
    self.assertFalse(self.capture.exists())

  def test_missing_value(self):
    result = self.run_wrapper("--restartdir")
    self.assertNotEqual(result.returncode, 0)
    self.assertIn("requires a directory", result.stdout)
    self.assertNotIn("Traceback", result.stderr)

  def test_restartdir_after_other_options(self):
    result = self.run_wrapper(
      "--restartdir", str(self.restart), "--ckptdir", str(self.restart))
    self.assert_success(result)
    args = self.captured()
    self.assertEqual(args[-4:], ["--ckptdir", str(self.restart),
                                "--restartdir", str(self.restart)])

  def test_default_current_directory(self):
    (self.cwd / "ckpt_rank_0").mkdir()
    self.assert_success(self.run_wrapper())
    args = self.captured()
    self.assertEqual(args[-2:], ["--restartdir", str(self.cwd)])
    self.assertEqual(args.count("--restartdir"), 1)

  def test_relative_directory(self):
    relative = os.path.relpath(self.restart, self.cwd)
    self.assert_success(self.run_wrapper("--restartdir", relative))
    self.assertEqual(self.captured()[-1], str(self.restart))

  def test_tilde_directory(self):
    self.assert_success(self.run_wrapper("--restartdir", "~/restart"))
    self.assertEqual(self.captured()[-1], str(self.restart))

  def test_regular_file_is_not_a_restart_directory(self):
    result = self.run_wrapper("--restartdir", str(self.restart / "README"))
    self.assertNotEqual(result.returncode, 0)
    self.assertIn("Restart directory doesn't exist", result.stdout)
    self.assertNotIn("Traceback", result.stderr)

  def test_own_header_uses_normalized_directory(self):
    self.assert_success(self.run_wrapper("--restartdir", "~/restart", rank=1))
    self.assertEqual(self.env_capture.read_text().splitlines(),
                     ["blocking", "11"])

  def test_all_headers_use_normalized_directory(self):
    relative = os.path.relpath(self.restart, self.cwd)
    self.assert_success(self.run_wrapper("--restartdir", relative))
    self.assertEqual(self.env_capture.read_text().splitlines(),
                     ["blocking", "10,11"])


if __name__ == "__main__":
  unittest.main()
