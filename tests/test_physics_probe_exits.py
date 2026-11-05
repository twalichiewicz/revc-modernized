"""The probe runner must distinguish completed physics rejections from crashes."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("physics_runner", ROOT / "tools/physics/check_conservation.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def process(code):
    return subprocess.run([sys.executable, "-c", code], capture_output=True, timeout=5).returncode


class ProbeExits(unittest.TestCase):
    def test_completed_acceptance(self):
        self.assertEqual(RUNNER.result_exit_code({"probe": process("raise SystemExit(0)")}), 0)

    def test_completed_physical_rejection(self):
        self.assertEqual(RUNNER.result_exit_code({"probe": process("raise SystemExit(1)")}), 1)

    def test_unexpected_process_exit_is_harness_failure(self):
        self.assertEqual(RUNNER.result_exit_code({"probe": process("raise SystemExit(7)")}), 2)

    def test_signal_is_harness_failure(self):
        terminated = process("import os,signal;os.kill(os.getpid(),signal.SIGTERM)")
        self.assertLess(terminated, 0)
        self.assertEqual(RUNNER.result_exit_code({"probe": terminated}), 2)

    def test_crash_takes_precedence_over_physical_rejection(self):
        terminated = process("import os,signal;os.kill(os.getpid(),signal.SIGTERM)")
        self.assertEqual(RUNNER.result_exit_code({"geometry": 1, "benchmark": terminated}), 2)


if __name__ == "__main__":
    unittest.main()
