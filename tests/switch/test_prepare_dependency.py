"""Integration tests using only local Git repositories and stdlib unittest."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
UTILITY = ROOT / "tools/switch/prepare_dependency.py"


class PrepareDependencyTests(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(os.environ.get("TMPDIR", ROOT / "build/tmp"))
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="prepare-dependency-", dir=temporary_root)
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name)
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith("GIT_")}
        self.environment.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                                GIT_TERMINAL_PROMPT="0")
        self.source = self.workspace / "source"
        self.source.mkdir()
        self.git(self.source, "init", "--quiet")
        (self.source / "dependency.txt").write_text("original\n")
        self.git(self.source, "add", "dependency.txt")
        self.git(self.source, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "--quiet", "-m", "initial")
        self.commit = self.git(self.source, "rev-parse", "HEAD").stdout.strip()
        self.patch = self.workspace / "dependency.patch"
        self.patch.write_text(
            "diff --git a/dependency.txt b/dependency.txt\n"
            "--- a/dependency.txt\n+++ b/dependency.txt\n"
            "@@ -1 +1 @@\n-original\n+prepared\n"
        )
        self.destination = self.workspace / "prepared"

    def git(self, directory, *arguments):
        return subprocess.run(
            ["git", "-c", f"core.hooksPath={os.devnull}", "-C", str(directory), *arguments],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
            env=self.environment,
        )

    def prepare(self, commit=None, source=None, environment=None):
        return subprocess.run(
            [sys.executable, "-B", str(UTILITY), str(source or self.source),
             self.commit if commit is None else commit, str(self.patch), str(self.destination)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=self.environment if environment is None else environment,
        )

    def snapshot(self, *roots):
        snapshot = {}
        for root in roots:
            for path in (root, *root.rglob("*")):
                metadata = path.lstat()
                contents = (os.readlink(path) if path.is_symlink()
                            else path.read_bytes() if path.is_file() else None)
                snapshot[str(path)] = (metadata.st_mode, metadata.st_mtime_ns, contents)
        return snapshot

    def test_tracked_source_subdirectory_is_refused_without_creating_destination(self):
        subdirectory = self.source / "tracked"
        subdirectory.mkdir()
        (subdirectory / "keep.txt").write_text("keep me\n")
        self.git(self.source, "add", "tracked")
        self.git(self.source, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "--quiet", "-m", "tracked subdirectory")
        self.commit = self.git(self.source, "rev-parse", "HEAD").stdout.strip()
        before = self.snapshot(self.source)
        for destination in (self.source / "prepared", self.workspace / "outside"):
            with self.subTest(destination=destination):
                self.destination = destination
                result = self.prepare(source=subdirectory)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.destination.exists())
                self.assertEqual(self.snapshot(self.source), before)
                self.assertIn("top-level", result.stderr)

    def test_destination_inside_external_git_directory_is_refused_without_source_changes(self):
        metadata = self.workspace / "external.git"
        self.git(self.source, "init", "--quiet", "--separate-git-dir", str(metadata))
        alias = self.workspace / "metadata-alias"
        alias.symlink_to(metadata, target_is_directory=True)
        before = self.snapshot(self.source, metadata)
        for parent in (metadata, alias):
            with self.subTest(parent=parent):
                self.destination = parent / "prepared"
                result = self.prepare()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.destination.exists())
                self.assertEqual(self.snapshot(self.source, metadata), before)
                self.assertIn("outside source", result.stderr)

    def test_linked_worktree_git_and_common_directories_are_protected(self):
        linked = self.workspace / "linked"
        self.git(self.source, "worktree", "add", "--quiet", "--detach", str(linked))
        private = Path(self.git(linked, "rev-parse", "--absolute-git-dir").stdout.strip())
        common = Path(self.git(linked, "rev-parse", "--path-format=absolute",
                               "--git-common-dir").stdout.strip())
        self.assertNotEqual(private, common)
        before = self.snapshot(self.source, linked)
        for parent in (private, common):
            with self.subTest(parent=parent):
                self.destination = parent / "prepared"
                result = self.prepare(source=linked)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.destination.exists())
                self.assertEqual(self.snapshot(self.source, linked), before)
                self.assertIn("outside source", result.stderr)

    def test_index_flags_are_refused_even_when_status_reports_clean(self):
        tracked = self.source / "dependency.txt"
        for flag in ("assume-unchanged", "skip-worktree"):
            self.git(self.source, "update-index", f"--{flag}", "dependency.txt")
            for modified in (False, True):
                with self.subTest(flag=flag, modified=modified):
                    tracked.write_text("hidden user work\n" if modified else "original\n")
                    self.assertEqual(self.git(self.source, "status", "--porcelain").stdout, "")
                    before = self.snapshot(self.source)
                    self.destination = self.workspace / f"{flag}-{modified}"
                    result = self.prepare()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(self.destination.exists())
                    self.assertEqual(self.snapshot(self.source), before)
                    self.assertIn("assume-unchanged or skip-worktree", result.stderr)
            self.git(self.source, "update-index", f"--no-{flag}", "dependency.txt")
            tracked.write_text("original\n")

    def test_valid_patch_creates_independent_checkout_with_receipt(self):
        before = {str(path.relative_to(self.source)): path.read_bytes()
                  for path in self.source.rglob("*") if path.is_file()}
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / "dependency.txt").read_text(), "prepared\n")
        self.assertEqual(self.git(self.destination, "rev-parse", "HEAD").stdout.strip(), self.commit)
        self.assertFalse((self.destination / ".git/objects/info/alternates").exists())
        source_objects = {path.stat().st_ino for path in (self.source / ".git/objects").rglob("*")
                          if path.is_file()}
        destination_objects = {path.stat().st_ino for path in (self.destination / ".git/objects").rglob("*")
                               if path.is_file()}
        self.assertFalse(source_objects & destination_objects, "Git objects must not be hardlinked")
        receipt = json.loads((self.destination / ".git/switch-dependency-receipt.json").read_text())
        self.assertEqual(receipt["source_commit"], self.commit)
        self.assertEqual(receipt["patch_sha256"], hashlib.sha256(self.patch.read_bytes()).hexdigest())
        after = {str(path.relative_to(self.source)): path.read_bytes()
                 for path in self.source.rglob("*") if path.is_file()}
        self.assertEqual(before, after, "Source worktree, Git metadata and refs must stay untouched")

    def test_dirty_source_is_rejected_without_changing_it(self):
        (self.source / "dependency.txt").write_text("user work\n")
        before = (self.source / "dependency.txt").read_bytes()
        for staged in (False, True):
            with self.subTest(staged=staged):
                if staged:
                    self.git(self.source, "add", "dependency.txt")
                result = self.prepare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("clean", result.stderr)
                self.assertFalse(self.destination.exists())
                self.assertEqual((self.source / "dependency.txt").read_bytes(), before)

    def test_dirty_submodule_is_rejected_even_when_source_config_hides_it(self):
        self.git(self.source, "-c", "protocol.file.allow=always", "submodule", "add",
                 "--quiet", str(self.source), "child")
        self.git(self.source, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-am", "add child", "--quiet")
        self.commit = self.git(self.source, "rev-parse", "HEAD").stdout.strip()
        self.git(self.source, "config", "submodule.child.ignore", "all")
        child_file = self.source / "child/dependency.txt"
        child_file.write_text("child user work\n")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean", result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertEqual(child_file.read_text(), "child user work\n")

    def test_untracked_source_files_are_rejected_even_when_status_hides_them(self):
        self.git(self.source, "config", "status.showUntrackedFiles", "no")
        (self.source / "untracked.txt").write_text("keep me\n")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean", result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertEqual((self.source / "untracked.txt").read_text(), "keep me\n")

    def test_ignored_build_files_are_also_rejected(self):
        (self.source / ".git/info/exclude").write_text("build/\n")
        (self.source / "build").mkdir()
        (self.source / "build/output.o").write_bytes(b"keep build output")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean", result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertEqual((self.source / "build/output.o").read_bytes(), b"keep build output")

    def test_existing_destination_is_never_reused_even_if_empty(self):
        for kind in ("empty-directory", "file", "dangling-symlink"):
            with self.subTest(kind=kind):
                self.destination = self.workspace / kind
                if kind == "empty-directory":
                    self.destination.mkdir()
                elif kind == "file":
                    self.destination.write_text("keep me\n")
                else:
                    self.destination.symlink_to(self.workspace / "missing")
                result = self.prepare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("destination", result.stderr)
                if kind == "empty-directory":
                    self.assertEqual(list(self.destination.iterdir()), [])
                elif kind == "file":
                    self.assertEqual(self.destination.read_text(), "keep me\n")
                else:
                    self.assertTrue(self.destination.is_symlink())

    def test_incompatible_patch_leaves_checkout_for_inspection_without_receipt(self):
        self.patch.write_text(self.patch.read_text().replace("-original", "-not present"))
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.destination / ".git").is_dir())
        self.assertEqual((self.destination / "dependency.txt").read_text(), "original\n")
        self.assertFalse((self.destination / ".git/switch-dependency-receipt.json").exists())
        self.assertEqual((self.source / "dependency.txt").read_text(), "original\n")
        self.assertIn("retained", result.stderr)
        self.assertIn(str(self.destination), result.stderr)

    def test_patch_path_escape_is_rejected_before_destination_creation(self):
        patches = []
        for path in ("../escape.txt", "nested/../../escape.txt", "/absolute.txt", "C:/escape.txt",
                     ".git/config", "nested/../dependency.txt", r"..\escape.txt"):
            patches.append((path,
                    f"diff --git a/{path} b/{path}\n"
                    f"--- a/{path}\n+++ b/{path}\n"
                    "@@ -1 +1 @@\n-original\n+escaped\n"
            ))
        patches.append(("quoted octal traversal",
                        'diff --git "a/\\056\\056/escape.txt" "b/\\056\\056/escape.txt"\n'
                        '--- "a/\\056\\056/escape.txt"\n+++ "b/\\056\\056/escape.txt"\n'
                        "@@ -1 +1 @@\n-original\n+escaped\n"))
        for operation in ("rename", "copy"):
            patches.append((f"unsafe {operation} source",
                            "diff --git a/../escape.txt b/safe.txt\nsimilarity index 100%\n"
                            f"{operation} from ../escape.txt\n{operation} to safe.txt\n"))
        for label, patch in patches:
            with self.subTest(path=label):
                self.patch.write_text(patch)
                result = self.prepare()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("unsafe patch path", result.stderr)
                self.assertFalse(self.destination.exists())
                self.assertFalse((self.workspace / "escape.txt").exists())
                self.assertEqual((self.source / "dependency.txt").read_text(), "original\n")

    def test_destination_inside_source_is_rejected(self):
        self.destination = self.source / "prepared"
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside source", result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertEqual(self.git(self.source, "status", "--porcelain").stdout, "")

    def test_ambient_git_environment_cannot_redirect_writes_to_source(self):
        environment = dict(self.environment, GIT_DIR=str(self.source / ".git"),
                           GIT_WORK_TREE=str(self.source))
        result = self.prepare(environment=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.source / "dependency.txt").read_text(), "original\n")
        self.assertEqual((self.destination / "dependency.txt").read_text(), "prepared\n")
        self.assertEqual(self.git(self.source, "status", "--porcelain").stdout, "")

    def test_clone_failure_preserves_destination_directory(self):
        broken_ref = self.source / ".git/refs/heads/broken"
        broken_ref.write_text("0" * 40 + "\n")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.destination.is_dir())
        self.assertIn("retained", result.stderr)
        self.assertEqual(broken_ref.read_text(), "0" * 40 + "\n")

    def test_uppercase_hex_pin_identifies_same_commit(self):
        result = self.prepare(commit=self.commit.upper())
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads((self.destination / ".git/switch-dependency-receipt.json").read_text())
        self.assertEqual(receipt["source_commit"], self.commit)

    def test_malformed_pin_is_rejected_before_repository_lookup(self):
        for pin in ("HEAD", self.commit[:12], "g" * 40, "0" * 41, "../HEAD", self.commit + "\n"):
            with self.subTest(pin=pin):
                result = self.prepare(commit=pin, source=self.workspace / "missing-source")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("40 hexadecimal", result.stderr)
                self.assertFalse(self.destination.exists())

    def test_wrong_revision_is_rejected_before_destination_creation(self):
        (self.source / "dependency.txt").write_text("later\n")
        self.git(self.source, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-am", "later", "--quiet")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("HEAD", result.stderr)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
