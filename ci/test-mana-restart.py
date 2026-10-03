#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "bin" / "mana_restart"

class ManaRestartTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mana-restart-test-")
        self.root = Path(self.temp.name)
        self.fake_bin = self.root / "fake-mana" / "bin"
        self.fake_bin.mkdir(parents=True)
        self.home = self.root / "home"
        self.home.mkdir()
        self.cwd = self.root / "unrelated"
        self.cwd.mkdir()
        self.restart = self.root / "restart"
        (self.restart / "ckpt_rank_0").mkdir(parents=True)
        (self.restart / "ckpt_rank_1").mkdir(parents=True)
        (self.restart / "ckpt_rank_0" / "ckpt_0.dmtcp").touch()
        (self.restart / "ckpt_rank_1" / "ckpt_1.dmtcp").touch()
        self.capture = self.root / "args.txt"
        shutil.copy2(WRAPPER, self.fake_bin / "mana_restart")
        os.chmod(self.fake_bin / "mana_restart", 0o755)
        self._exe(self.fake_bin / "dmtcp_command", "#!/bin/sh\nexit 0\n")
        self._exe(self.fake_bin / "lower-half", '#!/bin/sh\nprintf "%s\\n" "$@" > "$MANA_TEST_CAPTURE"\nexit 0\n')
        (self.home / ".mana.rc").write_text("Host: localhost\nPort: 7780\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def _exe(path, content):
        path.write_text(content, encoding="utf-8")
        os.chmod(path, 0o755)

    def run_wrapper(self, *args):
        env = os.environ.copy()
        env["HOME"] = str(self.home)
        env["MANA_TEST_CAPTURE"] = str(self.capture)
        env.pop("SLURM_JOB_ID", None)
        return subprocess.run([str(self.fake_bin / "mana_restart"), *args], cwd=self.cwd, env=env, text=True, capture_output=True, check=False, timeout=30)

    def captured(self):
        return self.capture.read_text(encoding="utf-8").splitlines()

    def test_outside_current_directory(self):
        result = self.run_wrapper("--verbose", "--restartdir", str(self.restart))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = self.captured()
        i = args.index("--restartdir")
        self.assertEqual(args[i + 1], str(self.restart.resolve()))

    def test_tmp_detection(self):
        (self.restart / "ckpt_rank_1" / "incomplete.tmp").touch()
        result = self.run_wrapper("--restartdir", str(self.restart))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Restart directory has .tmp files", result.stdout + result.stderr)

    def test_missing_value(self):
        result = self.run_wrapper("--restartdir")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires a directory", result.stdout + result.stderr)

    def test_restartdir_after_other_options(self):
        result = self.run_wrapper("--restartdir", str(self.restart), "--ckptdir", str(self.restart))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = self.captured()
        self.assertGreater(args.index("--restartdir"), args.index("--ckptdir"))

    def test_default_current_directory(self):
        (self.cwd / "ckpt_rank_0").mkdir()
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = self.captured()
        i = args.index("--restartdir")
        self.assertEqual(args[i + 1], str(self.cwd.resolve()))

if __name__ == "__main__":
    unittest.main()
