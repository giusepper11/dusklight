#!/usr/bin/env python3
"""Prepare a patched, independent local dependency checkout (stdlib + Git only).

Usage: python3 tools/switch/prepare_dependency.py SOURCE COMMIT PATCH DESTINATION

The destination is retained on failure for inspection. This tool never resets,
cleans, stashes, or modifies the source repository. No submodules are fetched.
The receipt lives in .git/switch-dependency-receipt.json, not the dependency tree.
Clean means no staged, unstaged, untracked or ignored files, including build output.
Use a quiescent source and a new destination with an existing parent directory.
SOURCE must be the top-level root of a nonbare Git worktree, not a subdirectory.
Destinations must be outside its worktree, Git directory and common Git directory.
Any assume-unchanged or skip-worktree index flags are refused, even on clean files.
Patches use Git's default -p1 path convention; unsafe/ambiguous paths are refused.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def git(*arguments, cwd=None, input=None):
    # An invoking shell's GIT_DIR/GIT_WORK_TREE/config must never redirect writes.
    environment = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    environment.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                       GIT_NO_LAZY_FETCH="1", GIT_TERMINAL_PROMPT="0")
    result = subprocess.run(
        ["git", "--no-optional-locks", "-c", f"core.hooksPath={os.devnull}",
         "-c", "core.fsmonitor=false", "-c", "protocol.allow=never",
         "-c", "protocol.file.allow=always", *map(str, arguments)],
        cwd=cwd, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
    )
    if result.returncode:
        raise ValueError(result.stderr.decode(errors="replace").strip())
    return result.stdout


def validate_patch_paths(patch_bytes, source):
    # Git decodes quoted/octal filenames for us. Reverse statistics also expose
    # old paths (deletions, renames). Check before and after the -p1 stripping
    # used for application: Git normalizes b//absolute differently with -p0.
    for direction in ((), ("--reverse",)):
        for strip in ("-p0", "-p1"):
            statistics = git("apply", "--numstat", "-z", strip, *direction, "-",
                             cwd=source, input=patch_bytes)
            for record in statistics.split(b"\0"):
                if not record:
                    continue
                path = record.split(b"\t", 2)[2]
                parts = path.lower().split(b"/")
                if (any(part in (b"", b".", b"..", b".git") for part in parts)
                        or b"\\" in path or b":" in path):
                    raise ValueError(f"unsafe patch path: {path!r}")


def prepare_dependency(source, commit, patch, destination):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        raise ValueError("commit must be exactly 40 hexadecimal characters")
    source = Path(source).resolve()
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"destination already exists: {destination}")
    worktree = Path(git("rev-parse", "--show-toplevel", cwd=source).decode().strip()).resolve()
    if source != worktree:
        raise ValueError("source must be the top-level root of a nonbare Git worktree")
    git_directory = Path(git("rev-parse", "--absolute-git-dir", cwd=source).decode().strip()).resolve()
    common_directory = Path(git("rev-parse", "--path-format=absolute", "--git-common-dir",
                                cwd=source).decode().strip()).resolve()
    if any(destination.resolve().is_relative_to(boundary)
           for boundary in (worktree, git_directory, common_directory)):
        raise ValueError("destination must be outside source repository and Git metadata")
    head = git("rev-parse", "HEAD", cwd=source).decode().strip()
    if head != commit.lower():
        raise ValueError(f"source HEAD {head} does not match requested commit {commit}")
    # Status can hide tracked changes behind these flags. Inspect without
    # clearing flags or refreshing the source index, including sparse checkouts.
    entries = git("ls-files", "-v", "-z", cwd=source).split(b"\0")
    if any(entry[:1] == b"S" or entry[:1].islower() for entry in entries):
        raise ValueError("source index must not use assume-unchanged or skip-worktree flags")
    if git("status", "--porcelain=v1", "-z", "--untracked-files=all",
           "--ignored=matching", "--ignore-submodules=none", cwd=source):
        raise ValueError("source repository must be clean")
    patch_bytes = Path(patch).read_bytes()
    validate_patch_paths(patch_bytes, source)
    # Reserve exclusively; Git otherwise removes a newly created directory when
    # clone fails. Missing parents are not created outside the supplied target.
    destination.mkdir()
    try:
        git("clone", "--no-local", "--no-checkout", "--", source, destination)
        git("checkout", "--detach", head, cwd=destination)
        git("apply", "--check", "-", cwd=destination, input=patch_bytes)
        git("apply", "-", cwd=destination, input=patch_bytes)
        receipt = {"source_commit": head, "patch_sha256": hashlib.sha256(patch_bytes).hexdigest()}
        (destination / ".git/switch-dependency-receipt.json").write_text(
            json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
        )
    except (ValueError, OSError) as error:
        raise ValueError(
            f"{error}\nAny failed destination is retained for inspection: {destination}"
        ) from error
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("commit")
    parser.add_argument("patch", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    try:
        destination = prepare_dependency(arguments.source, arguments.commit,
                                         arguments.patch, arguments.destination)
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Prepared dependency: {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
