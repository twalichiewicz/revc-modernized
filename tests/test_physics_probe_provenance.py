"""Exercise the probe's source record with real local git clones and patch replay.

Only the unrelated, expensive CMake boundary is stopped; the runner generates
its real artifacts, and a second checkout independently replays those artifacts.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("conservation_runner", ROOT / "tools/physics/check_conservation.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
PRISTINE = ROOT / "build/world-physics-evaluation/bullet3"


class StopAtBuild(Exception):
    pass


class Provenance(unittest.TestCase):
    def setUp(self):
        if not (PRISTINE / ".git").exists():
            self.skipTest("Requires the pinned local Bullet checkout; no network fetch is performed")
        self.temporary = tempfile.TemporaryDirectory(prefix="physics-provenance-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "source"
        self.output = self.directory / "output"
        self.clone(self.source)

    def clone(self, target):
        subprocess.run(["git", "clone", "--shared", "--no-hardlinks", str(PRISTINE), str(target)],
                       capture_output=True, check=True)

    def generate_record(self):
        real_run = subprocess.run

        def stop_build(command, *args, **kwargs):
            if command[0] == "cmake":
                raise StopAtBuild()
            return real_run(command, *args, **kwargs)

        arguments = ["check_conservation.py", "--source", str(self.source),
                     "--build-dir", str(self.directory / "build"), "--output-dir", str(self.output)]
        with patch.object(sys, "argv", arguments), patch.object(RUNNER.subprocess, "run", stop_build):
            with self.assertRaises(StopAtBuild):
                RUNNER.main()

    def assert_replay(self, filenames):
        self.generate_record()
        replay = self.directory / "replay"
        self.clone(replay)
        recorded = self.output / "source.patch"
        if recorded.read_bytes():
            subprocess.run(["git", "-C", str(replay), "apply", "--binary", str(recorded)],
                           capture_output=True, check=True)
        for filename in filenames:
            self.assertTrue((replay / filename).exists(), f"The recorded patch omits {filename}")
            self.assertEqual((replay / filename).read_bytes(), (self.source / filename).read_bytes(),
                             f"The recorded patch does not reconstruct {filename}")

    def test_staged_source_reconstructs(self):
        filename = "src/BulletSoftBody/btSoftBody.h"
        target = self.source / filename
        target.write_bytes(b"// staged provenance fixture\n" + target.read_bytes())
        subprocess.run(["git", "-C", str(self.source), "add", filename], check=True)
        self.assert_replay([filename])

    def test_mixed_staged_and_working_source_reconstructs(self):
        first = "src/BulletSoftBody/btSoftBody.h"
        second = "src/BulletSoftBody/btSoftBodyInternals.h"
        for filename in (first, second):
            target = self.source / filename
            target.write_bytes(b"// mixed provenance fixture\n" + target.read_bytes())
        subprocess.run(["git", "-C", str(self.source), "add", first], check=True)
        self.assert_replay([first, second])

    def test_working_source_reconstructs(self):
        filename = "src/BulletSoftBody/btSoftBody.h"
        target = self.source / filename
        target.write_bytes(b"// unstaged provenance fixture\n" + target.read_bytes())
        self.assert_replay([filename])

    def test_indexed_new_source_reconstructs(self):
        filename = "src/BulletSoftBody/recorded_material.h"
        (self.source / filename).write_text("// indexed material fixture\n")
        subprocess.run(["git", "-C", str(self.source), "add", filename], check=True)
        self.assert_replay([filename])

    def test_untracked_source_is_rejected(self):
        (self.source / "unrecorded_material.h").write_text("// not captured by a git patch\n")
        with self.assertRaisesRegex(RuntimeError, "[Uu]ntracked"):
            self.generate_record()


if __name__ == "__main__":
    unittest.main()
