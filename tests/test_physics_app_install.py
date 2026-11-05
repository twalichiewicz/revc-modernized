"""Exercise real directory swaps and rollback without touching Applications."""
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("physics_app", Path(__file__).resolve().parents[1] / "tools/host/app_package.py")
app_package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(app_package)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.app = self.root / "Game.app"; self.stage = self.root / "Stage.app"; self.backup = self.root / "Backup.app"
        for folder in [self.app, self.stage]:
            (folder / "Contents/MacOS").mkdir(parents=True); (folder / "Contents/Frameworks").mkdir()
            (folder / "Contents/MacOS/macVC").write_bytes(b"game")
            (folder / "Contents/Frameworks/libVCModernHUD.dylib").write_bytes(b"mod")
            (folder / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "macVC"}))
        (self.stage / "Contents/MacOS/vcLauncher").write_bytes(b"launcher")
        self.expected = app_package.fingerprint(self.app)

    def tearDown(self): self.tmp.cleanup()

    def test_success_preserves_original_app(self):
        app_package.install(self.stage, self.app, self.backup, self.expected, lambda p: None)
        self.assertTrue((self.app / "Contents/MacOS/vcLauncher").exists())
        self.assertEqual(app_package.fingerprint(self.backup), self.expected)

    def test_validation_failure_restores_working_app(self):
        def fail(path): raise RuntimeError("smoke failed")
        with self.assertRaisesRegex(RuntimeError, "smoke failed"):
            app_package.install(self.stage, self.app, self.backup, self.expected, fail)
        self.assertEqual(app_package.fingerprint(self.app), self.expected)
        self.assertFalse((self.app / "Contents/MacOS/vcLauncher").exists())

    def test_changed_game_refuses_install_before_mutation(self):
        (self.app / "Contents/MacOS/macVC").write_bytes(b"new user build")
        with self.assertRaisesRegex(RuntimeError, "changed"):
            app_package.install(self.stage, self.app, self.backup, self.expected, lambda p: None)
        self.assertTrue(self.stage.exists()); self.assertFalse(self.backup.exists())

    def test_existing_backup_is_never_overwritten(self):
        self.backup.mkdir(); (self.backup / "precious").write_text("keep")
        with self.assertRaises(FileExistsError):
            app_package.install(self.stage, self.app, self.backup, self.expected, lambda p: None)
        self.assertEqual((self.backup / "precious").read_text(), "keep")
        self.assertTrue(self.stage.exists())

    def test_staged_game_bytes_must_match_original(self):
        (self.stage / "Contents/MacOS/macVC").write_bytes(b"wrong host")
        with self.assertRaisesRegex(RuntimeError, "normal game"):
            app_package.install(self.stage, self.app, self.backup, self.expected, lambda p: None)
        self.assertFalse(self.backup.exists()); self.assertTrue(self.app.exists())

    def test_changed_validated_payload_refuses_swap(self):
        payload={"Contents/MacOS/vcLauncher":app_package.digest(self.stage/"Contents/MacOS/vcLauncher")}
        (self.stage/"Contents/MacOS/vcLauncher").write_bytes(b"different launcher")
        with self.assertRaisesRegex(RuntimeError,"payload"):
            app_package.install(self.stage,self.app,self.backup,self.expected,lambda p:None,payload=payload)
        self.assertFalse(self.backup.exists())

    def test_explicit_restore_keeps_experiment_and_restores_original(self):
        app_package.install(self.stage,self.app,self.backup,self.expected,lambda p:None)
        archive=self.root/"Experiment.app"
        app_package.restore(self.app,self.backup,archive,app_package.fingerprint(self.app),self.expected)
        self.assertEqual(app_package.fingerprint(self.app),self.expected)
        self.assertTrue((archive/"Contents/MacOS/vcLauncher").exists())

    def test_recovery_record_exists_before_swap_and_validation(self):
        record = self.root / "package.json"
        manifest = {"original": self.expected}
        def verify(path):
            saved = json.loads(record.read_text())
            self.assertEqual(saved["backup"], str(self.backup))
            self.assertEqual(saved["installed_fingerprint"], app_package.fingerprint(path))
        app_package.install_recorded(self.stage, self.app, self.backup, record, manifest, verify)
        saved = json.loads(record.read_text())
        app_package.restore(self.app, Path(saved["backup"]), self.root/"Archive.app", saved["installed_fingerprint"], saved["original"])
        self.assertEqual(app_package.fingerprint(self.app), self.expected)

    def test_failed_journal_commit_prevents_swap_and_preserves_record(self):
        record = self.root / "package.json"
        record.write_text('{"previous":"record"}')
        with patch.object(app_package.os, "replace", side_effect=OSError("disk write failed")):
            with self.assertRaisesRegex(OSError, "disk write failed"):
                app_package.install_recorded(self.stage, self.app, self.backup, record, {"original":self.expected}, lambda p:None)
        self.assertEqual(record.read_text(), '{"previous":"record"}')
        self.assertFalse(self.backup.exists())
        self.assertEqual(app_package.fingerprint(self.app), self.expected)

    def test_recovery_metadata_survives_post_swap_write_failure(self):
        record = self.root / "package.json"
        manifest = {"original":self.expected}
        def fail_later(path):
            with patch.object(app_package.os, "replace", side_effect=OSError("disk full after swap")):
                with self.assertRaisesRegex(OSError,"disk full after swap"):
                    app_package.atomic_record(record,{"invalid":"incomplete replacement"})
        app_package.install_recorded(self.stage,self.app,self.backup,record,manifest,fail_later)
        saved = json.loads(record.read_text())
        app_package.restore(self.app,Path(saved["backup"]),self.root/"Archive.app",saved["installed_fingerprint"],saved["original"])
        self.assertEqual(app_package.fingerprint(self.app), self.expected)

    def test_interrupted_swap_can_restore_recorded_backup_when_app_missing(self):
        self.app.rename(self.backup)
        app_package.restore(self.app,self.backup,self.root/"Archive.app",self.expected,self.expected)
        self.assertEqual(app_package.fingerprint(self.app),self.expected)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "native Cocoa launcher requires macOS CLT")
class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.build.name) / "launcher"
        source = Path(__file__).resolve().parents[1] / "tools/host/launcher.mm"
        subprocess.run(["xcrun", "clang++", "-std=c++17", "-fobjc-arc", str(source), "-framework", "Cocoa", "-framework", "ApplicationServices", "-o", str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.app = Path(self.tmp.name).resolve() / "Game With Spaces.app"
        binaries = self.app / "Contents/MacOS"
        binaries.mkdir(parents=True)
        self.launcher = binaries / "vcLauncher"
        shutil.copy2(self.binary, self.launcher)

    def tearDown(self): self.tmp.cleanup()

    def test_normal_arguments_are_preserved(self):
        result = subprocess.run([str(self.launcher), "--check-launcher", "file with spaces", "--unknown-native-option"], capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout.splitlines(), [str(self.app / "Contents/MacOS/macVC"), "file with spaces", "--unknown-native-option"])

    def test_lab_resolves_only_bundle_local_resources(self):
        result = subprocess.run([str(self.launcher), "--check-launcher", "--physics-test"], capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout.splitlines(), [str(self.app / "Contents/MacOS/vcPhysicsHost"), "--wing-lab", str(self.app / "Contents/Resources/PhysicsTest/aircraft.txt")])

    def test_missing_lab_assets_fail_before_exec(self):
        (self.app / "Contents/MacOS/vcPhysicsHost").write_bytes(b"not executable")
        result = subprocess.run([str(self.launcher), "--physics-test"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Required game/test files are missing", result.stderr)


if __name__ == "__main__": unittest.main()
