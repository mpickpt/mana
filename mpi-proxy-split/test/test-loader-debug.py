#!/usr/bin/env python3
"""Exercise the production loader lookup using private ELF/debug fixtures."""

import os
from pathlib import Path
import re
import resource
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "lower-half"
DRIVER = Path(__file__).with_name("loader-debug-driver.cpp")


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True)


def disable_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class LoaderDebugTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="mana-loader-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        cls.helper = cls.work / "lookup"
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        cc = shlex.split(os.environ.get("CC", "cc"))
        run(*cxx, "-std=c++11", "-g", "-I", str(SOURCE),
            str(SOURCE / "build-id-debug.cpp"),
            str(SOURCE / "get-symbol-offset.c"), str(DRIVER),
            "-o", str(cls.helper))
        source = cls.work / "fixture.c"
        source.write_text("int mana_fixture_symbol(void) { return 7; }\n"
                          "int main(void) { return mana_fixture_symbol(); }\n")
        cls.original = cls.work / "fixture"
        run(*cc, "-g", "-Wl,--build-id=sha1", str(source),
            "-o", str(cls.original))
        cls.no_id = cls.work / "no-id"
        run(*cc, "-g", "-Wl,--build-id=none", str(source),
            "-o", str(cls.no_id))
        cls.debug = cls.work / "fixture.debug"
        run("objcopy", "--only-keep-debug", str(cls.original), str(cls.debug))
        cls.stripped = cls.work / "stripped"
        shutil.copyfile(cls.original, cls.stripped)
        run("strip", "--strip-all", str(cls.stripped))
        notes = run("readelf", "-n", str(cls.original)).stdout
        cls.build_id = re.search(r"Build ID:\s*([0-9a-f]+)", notes)[1]
        symbols = run("readelf", "-sW", str(cls.original)).stdout
        cls.expected = next(int(line.split()[1], 16)
                            for line in symbols.splitlines()
                            if line.split() and
                            line.split()[-1] == "mana_fixture_symbol")

    def setUp(self):
        self.case = tempfile.TemporaryDirectory(dir=self.work)
        self.addCleanup(self.case.cleanup)
        self.root = Path(self.case.name) / "debug"
        self.root.mkdir()

    def path(self, elf, root=None):
        return run(str(self.helper), "path", str(elf),
                   str(root or self.root)).stdout.strip()

    def lookup(self, elf):
        return int(run(str(self.helper), "lookup", str(elf),
                       "mana_fixture_symbol", str(self.root)).stdout.strip())

    def install_debug(self):
        target = self.root / ".build-id" / self.build_id[:2]
        target.mkdir(parents=True)
        target = target / (self.build_id[2:] + ".debug")
        shutil.copyfile(self.debug, target)
        return target

    def malformed(self, data):
        path = Path(self.case.name) / "bad-elf"
        path.write_bytes(data)
        return self.path(path)

    def test_build_id_path(self):
        expected = self.root / ".build-id" / self.build_id[:2]
        expected = expected / (self.build_id[2:] + ".debug")
        self.assertEqual(self.path(self.original), str(expected))

    def test_trailing_root_slashes(self):
        self.assertEqual(self.path(self.original),
                         self.path(self.original, str(self.root) + "///"))

    def test_symlink_build_id(self):
        link = Path(self.case.name) / "loader"
        link.symlink_to(self.stripped)
        self.assertEqual(self.path(link), self.path(self.stripped))

    def test_symbols_in_original_file(self):
        self.assertEqual(self.lookup(self.original), self.expected)

    def test_conventional_debug_path(self):
        link = Path(self.case.name) / "loader"
        link.symlink_to(self.stripped)
        target = Path(str(self.root) + str(self.stripped))
        target.parent.mkdir(parents=True)
        shutil.copyfile(self.debug, target)
        self.assertEqual(self.lookup(link), self.expected)

    def test_build_id_fallback_from_symlink(self):
        self.install_debug()
        link = Path(self.case.name) / "loader"
        link.symlink_to(self.stripped)
        self.assertEqual(self.lookup(link), self.expected)

    def test_build_id_fallback_from_regular_file(self):
        self.install_debug()
        self.assertEqual(self.lookup(self.stripped), self.expected)

    def test_missing_debug_file_reports_attempted_path(self):
        result = subprocess.run(
            [str(self.helper), "lookup", str(self.stripped),
             "mana_fixture_symbol", str(self.root)], text=True,
            capture_output=True, preexec_fn=disable_core)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(self.path(self.stripped), result.stderr)
        self.assertIn("Unable to find symbol", result.stderr)

    def test_missing_file(self):
        self.assertEqual(self.path(self.root / "absent"), "")

    def test_non_elf_file(self):
        self.assertEqual(self.malformed(b"not an ELF file\n"), "")

    def test_truncated_header(self):
        self.assertEqual(self.malformed(self.original.read_bytes()[:32]), "")

    def test_unsupported_class_and_byte_order(self):
        other_endian = 2 if sys.byteorder == "little" else 1
        for index, value in [(4, 1), (5, other_endian)]:
            with self.subTest(index=index):
                data = bytearray(self.original.read_bytes())
                data[index] = value
                self.assertEqual(self.malformed(data), "")

    def test_program_headers_outside_file(self):
        data = bytearray(self.original.read_bytes())
        byte_order = "<" if sys.byteorder == "little" else ">"
        struct.pack_into(byte_order + "Q", data, 32, len(data) + 4096)
        self.assertEqual(self.malformed(data), "")

    def test_truncated_note_segments(self):
        data = bytearray(self.original.read_bytes())
        byte_order = "<" if sys.byteorder == "little" else ">"
        phoff = struct.unpack_from(byte_order + "Q", data, 32)[0]
        count = struct.unpack_from(byte_order + "H", data, 56)[0]
        for index in range(count):
            offset = phoff + index * 56
            if struct.unpack_from(byte_order + "I", data, offset)[0] == 4:
                struct.pack_into(byte_order + "Q", data, offset + 32,
                                 len(data) + 4096)
        self.assertEqual(self.malformed(data), "")

    def test_elf_without_build_id(self):
        self.assertEqual(self.path(self.no_id), "")


if __name__ == "__main__":
    unittest.main()
