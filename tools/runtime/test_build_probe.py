#!/usr/bin/env python3
"""Build policy tests: current mirrors must not describe an older plugin."""
import os
from pathlib import Path
import tempfile
import unittest

from build_probe import stale_abi_sources


class AbiFreshnessTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "src").mkdir()
        self.plugin = self.root / "plugin.dylib"
        self.plugin.write_bytes(b"fixture")
        os.utime(self.plugin, ns=(2_000_000_000, 2_000_000_000))

    def source(self, name, timestamp):
        path = self.root / "src" / name
        path.write_text("fixture")
        os.utime(path, ns=(timestamp, timestamp))
        return path

    def test_rejects_changed_body_header(self):
        header = self.source("reactphysics.h", 3_000_000_000)
        self.assertEqual(stale_abi_sources(self.plugin, self.root), [header])

    def test_rejects_new_reaction_module_source(self):
        source = self.source("reactself.cpp", 3_000_000_000)
        self.assertEqual(stale_abi_sources(self.plugin, self.root), [source])

    def test_accepts_sources_no_newer_than_binary(self):
        self.source("reactphysics.h", 1_000_000_000)
        self.source("reactions.cpp", 2_000_000_000)
        self.assertEqual(stale_abi_sources(self.plugin, self.root), [])

    def test_checks_other_mirrored_plugin_layouts(self):
        paths = [self.source(name, 3_000_000_000) for name in ("heli.cpp", "config.h", "heat.h", "sight.h", "jetwheels.h", "jetwheels.cpp")]
        self.assertEqual(stale_abi_sources(self.plugin, self.root), sorted(paths))


if __name__ == "__main__":
    unittest.main()
