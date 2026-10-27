#!/usr/bin/env python3
"""Fetch the pinned private Asterwave source using the current Git credentials.

Run explicitly before project generation. --check performs offline validation only.
The checkout is kept outside tracked LuxEngine sources; no token is stored in a URL.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def load_manifest(repository_root=REPOSITORY_ROOT):
    manifest = json.loads((Path(repository_root) / "scripts/dependencies/Asterwave.json").read_text(encoding="utf-8"))
    if not re.fullmatch(r"[0-9a-f]{40}", manifest["revision"]):
        raise ValueError("Asterwave manifest must pin a full commit SHA")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", manifest["repository"]):
        raise ValueError("Invalid Asterwave repository in manifest")
    for name in manifest["sources"] + manifest["required_files"]:
        if Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError(f"Invalid Asterwave manifest path: {name}")
    return manifest


def source_root(repository_root=REPOSITORY_ROOT):
    return Path(os.getenv("LUX_ASTERWAVE_ROOT") or Path(repository_root) / "Core/vendor/Asterwave").resolve()


def git(root, *arguments, required=True):
    result = subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True)
    if result.returncode:
        if not required:
            return None
        raise RuntimeError(result.stderr.strip() or f"git {' '.join(arguments)} failed in {root}")
    return result.stdout.strip()


def validate_checkout(root, manifest):
    root = Path(root).resolve()
    for name in manifest["sources"] + manifest["required_files"]:
        if not (root / name).is_file():
            raise RuntimeError(f"Required Asterwave source file missing: {root / name}")
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise RuntimeError(f"Asterwave must be its own Git checkout: {root}")
    actual = git(root, "rev-parse", "HEAD")
    if actual != manifest["revision"]:
        raise RuntimeError(f"Asterwave revision mismatch: expected {manifest['revision']}, found {actual} in {root}")
    if git(root, "status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError(f"Asterwave checkout has local changes: {root}. Preserve them separately before using the pinned dependency.")


def fetch(root, manifest):
    root = Path(root).resolve()
    if REPOSITORY_ROOT.is_relative_to(root):
        raise RuntimeError(f"Refusing to replace the LuxEngine checkout or its parent: {root}")
    if root.is_relative_to(REPOSITORY_ROOT):
        # Check a prospective source file rather than a trailing directory separator: Git
        # for Windows can report an empty ignore pattern as a match for the latter.
        relative = (root.relative_to(REPOSITORY_ROOT) / "src/simulation.cpp").as_posix()
        ignored = subprocess.run(["git", "-C", str(REPOSITORY_ROOT), "check-ignore", "--quiet", "--no-index", relative])
        if ignored.returncode:
            raise RuntimeError(f"Private source destination must be gitignored or outside LuxEngine: {root}")
    if root.exists() and any(root.iterdir()):
        if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
            raise RuntimeError(f"Refusing to overwrite a non-checkout directory: {root}")
        if git(root, "status", "--porcelain", "--untracked-files=all"):
            raise RuntimeError(f"Refusing to change a dirty Asterwave checkout: {root}")
        # A failed first fetch leaves an initialized repository with no HEAD; permit a retry.
        current = git(root, "rev-parse", "--verify", "HEAD", required=False)
        if current == manifest["revision"]:
            validate_checkout(root, manifest)
            return
        if current and not (root / "include/asterwave/simulation.hpp").is_file():
            raise RuntimeError(f"Refusing to replace an unrelated Git checkout: {root}")
    else:
        root.mkdir(parents=True, exist_ok=True)
        git(root, "init")
    # An explicit URL avoids fetching from a stale or repointed local remote. Authentication
    # comes from Git's credential helper or SSH configuration, never command-line token text.
    git(root, "fetch", "--depth=1", f"https://github.com/{manifest['repository']}.git", manifest["revision"])
    git(root, "checkout", "--detach", manifest["revision"])
    validate_checkout(root, manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, help="Checkout directory (default: LUX_ASTERWAVE_ROOT or Core/vendor/Asterwave).")
    parser.add_argument("--check", action="store_true", help="Validate the checkout without network access or changes.")
    args = parser.parse_args()
    try:
        manifest = load_manifest()
        root = args.root.resolve() if args.root else source_root()
        if args.check:
            validate_checkout(root, manifest)
        else:
            fetch(root, manifest)
        print(f"Asterwave {manifest['revision']} ready: {root}")
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        print("Run python scripts/FetchAsterwave.py with Git credentials that can read the private repository.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
