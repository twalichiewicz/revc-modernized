"""Real local repositories exercise source reconstruction for the CCD runner."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("continuous_runner", ROOT / "tools/physics/check_continuous.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class ContinuousProvenance(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="continuous-record-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.git("init", "-q")
        (self.source / "material.h").write_text("original\n")
        self.git("add", "material.h")
        self.git("-c", "user.name=Physics fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture")
        self.head = self.git("rev-parse", "HEAD").stdout.strip()

    def git(self, *arguments):
        return subprocess.run(["git", "-C", str(self.source), *arguments], check=True, capture_output=True, text=True)

    def test_staged_and_working_input_reconstructs(self):
        (self.source / "material.h").write_text("staged\n")
        self.git("add", "material.h")
        (self.source / "material.h").write_text("staged plus current\n")
        record = self.root / "source.patch"
        data = RUNNER.source_record(self.source, record, self.head)
        replay = self.root / "replay"
        subprocess.run(["git", "clone", "--shared", str(self.source), str(replay)], capture_output=True, check=True)
        subprocess.run(["git", "-C", str(replay), "apply", "--binary", str(record)], capture_output=True, check=True)
        self.assertEqual((replay / "material.h").read_text(), "staged plus current\n")
        self.assertEqual(data["revision"], self.head)

    def test_untracked_source_rejected(self):
        (self.source / "new_force.h").write_text("new input\n")
        with self.assertRaisesRegex(RuntimeError, "Untracked"):
            RUNNER.source_record(self.source, self.root / "source.patch", self.head)

    def test_wrong_pin_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Expected"):
            RUNNER.source_record(self.source, self.root / "source.patch", "0" * 40)

    def test_symlink_and_regular_file_are_different_inputs(self):
        archive = self.root / "archive"
        archive.mkdir()
        (archive / "target.h").write_text("#define VALUE 7\n")
        (archive / "force.h").write_text("target.h")
        regular = RUNNER.tree_hash(archive)
        (archive / "force.h").unlink()
        (archive / "force.h").symlink_to("target.h")
        linked = RUNNER.tree_hash(archive)
        self.assertNotEqual(regular["tree_sha256"], linked["tree_sha256"])

    def test_cached_build_is_bound_to_recorded_source(self):
        actual = self.root / "actual-source"
        actual.mkdir()
        (actual / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.24)\nproject(Fixture NONE)\noption(USE_DOUBLE_PRECISION \"fixture\" ON)\n")
        build = self.root / "build"
        subprocess.run(["cmake", "-S", str(actual), "-B", str(build)], capture_output=True, check=True)
        RUNNER.validate_bullet_build(actual, build)
        with self.assertRaisesRegex(RuntimeError, "source"):
            RUNNER.validate_bullet_build(self.source, build)

    def test_archive_fingerprint_detects_input_change(self):
        archive = self.root / "archive"
        archive.mkdir()
        (archive / "force.h").write_text("before\n")
        before = RUNNER.tree_hash(archive)
        (archive / "force.h").write_text("after\n")
        after = RUNNER.tree_hash(archive)
        self.assertNotEqual(before["tree_sha256"], after["tree_sha256"])
        self.assertEqual(after["files"], 1)


if __name__ == "__main__":
    unittest.main()
