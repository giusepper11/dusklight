import importlib.util
from pathlib import Path
import struct
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "performance_artifact", ROOT / "platforms/switch/performance-monitor/verify_artifact.py")
assert SPEC is not None and SPEC.loader is not None
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)
BASELINE = ROOT.parent / "snapshots/nvk-reference-00b7271-shallow/build/switch-nvk-upstream-0ff5207-release/platforms/switch/dusklight.nro"


class ArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = BASELINE.read_bytes()  # Preserved REAL baseline, never modified.

    def test_real_baseline_package(self):
        report, files = artifact.inspect_nro(self.data)
        self.assertEqual(report["nacp_title"], "Dusklight")
        self.assertEqual(report["nacp_size"], 0x4000)
        self.assertIn("res/rml/overlay.rcss", files)
        self.assertGreater(len(files["res/rml/overlay.rcss"]), 0)

    def test_truncated_nro(self):
        with self.assertRaises(ValueError):
            artifact.inspect_nro(self.data[:0x80])

    def test_invalid_nro_magic(self):
        with self.assertRaises(ValueError):
            artifact.inspect_nro(bytes(0x80))

    def test_invalid_romfs(self):
        with self.assertRaises(ValueError):
            artifact.romfs_files(bytes(0x80))

    def test_corrupt_romfs_offsets(self):
        malformed = bytearray(0x80)
        struct.pack_into("<Q", malformed, 0, 0x50)
        struct.pack_into("<Q", malformed, 0x18, 0xffffffffffffffff)
        with self.assertRaises(ValueError):
            artifact.romfs_files(malformed)

    def test_invalid_elf(self):
        with self.assertRaises(ValueError):
            artifact.inspect_elf(bytes(64))

    def test_repack_binds_real_baseline_elf_to_nro(self):
        build = BASELINE.parents[2]
        output = ROOT / "build/performance-monitor-host/artifact-fixtures" / str(time.time_ns())
        output.mkdir(parents=True, exist_ok=False)
        artifact.verify_nro_matches_elf(
            self.data, build / "dusklight.elf", BASELINE.parent,
            build.parents[1] / "platforms/switch/icon.jpg",
            Path("/opt/devkitpro/tools/bin/elf2nro"), output)
        changed = bytearray(self.data)
        changed[0x1000] ^= 1  # In-memory negative fixture, original never changed.
        with self.assertRaises(ValueError):
            artifact.require_matching_package(changed, (output / "validation-rebuilt.nro").read_bytes())

    def test_untracked_build_input_rejected(self):
        fixture = ROOT / "build/performance-monitor-host/git-fixtures" / str(time.time_ns())
        fixture.mkdir(parents=True, exist_ok=False)
        subprocess.run(["git", "init", "--quiet", str(fixture)], check=True)
        artifact.require_clean_source(fixture)
        (fixture / "untracked.hpp").write_text("// synthetic host fixture\n")
        with self.assertRaises(ValueError):
            artifact.require_clean_source(fixture)

    def test_packaged_configuration_guard(self):
        fixture = ROOT / "build/performance-monitor-host/lock-fixtures" / str(time.time_ns())
        fixture.mkdir(parents=True, exist_ok=False)
        lock = fixture / "packaged.json"
        command = ["cmake", f"-DLOCK_FILE={lock}", "-P",
                   str(ROOT / "platforms/switch/cmake/CheckPerformanceBuildUnlocked.cmake")]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        lock.write_text('{"synthetic_fixture": true}\n')
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
