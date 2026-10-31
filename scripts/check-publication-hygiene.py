#!/usr/bin/env python3
"""Reject tracked files that expose developer home-directory paths."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


POSIX_ROOT = "/"

PRIVATE_HOME_PATTERNS = (
    (
        "macOS user home",
        re.compile(
            POSIX_ROOT
            + r"Users/(?!(?:<user>|runner|Shared)(?=/|[\s\"'`,:}\]]|$))"
            r"[^/\s\"'`]+"
        ),
    ),
    (
        "Linux user home",
        re.compile(
            POSIX_ROOT
            + r"home/(?!(?:user|runner|x|web_user|chrome)"
            r"(?=/|[\s\"'`,:}\]]|$))[^/\s\"'`]+"
        ),
    ),
    (
        "Windows user home",
        re.compile(
            r"[A-Za-z]:\\Users\\"
            r"(?!<user>(?:\\|$)|runner(?:\\|$)|Public(?:\\|$))"
            r"[^\\\s\"'`]+"
        ),
    ),
)

# These exact files are byte-bound evidence. Rewriting embedded machine paths
# would invalidate their recorded identities; neighboring and future evidence
# remains in the scan.
IMMUTABLE_RAW_EVIDENCE_FILES = frozenset(
    {
        "bench/out/browser-promotion/20260724T204526Z/browser_gate.json",
        (
            "browser/chromium/artifacts/20260724T204526Z/"
            "dawn-vs-doe.browser-layered.superset.diagnostic.json"
        ),
        (
            "browser/chromium/artifacts/20260724T204526Z/"
            "dawn-vs-doe.browser-layered.superset.summary.json"
        ),
        (
            "browser/chromium/artifacts/20260724T204526Z/"
            "dawn-vs-doe.browser.playwright-smoke.diagnostic.json"
        ),
        "reports/maintenance/20261011-intel-vulkan-projection/cohort.json",
        "reports/maintenance/20261011-intel-vulkan-projection/dawn.json",
        "reports/maintenance/20261011-intel-vulkan-projection/doe.json",
        "reports/maintenance/20261011-intel-vulkan-projection/manifest.json",
        "reports/maintenance/20261011-intel-vulkan-projection/matrix.json",
    }
)
PUBLIC_CORPUS_PREFIXES = ("pipeline/dawn-research/data/",)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    label: str


def list_tracked_files() -> list[Path]:
    """Return every path tracked by the current Git worktree."""

    result = subprocess.run(
        ["git", "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    paths = [Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw]
    return [
        path
        for path in paths
        if path.as_posix() not in IMMUTABLE_RAW_EVIDENCE_FILES
        and not any(
            path.as_posix().startswith(prefix) for prefix in PUBLIC_CORPUS_PREFIXES
        )
    ]


def scan_file(path: Path) -> list[Finding]:
    """Return private-home findings for one tracked text file."""

    if not path.is_file():
        return []
    payload = path.read_bytes()
    if b"\0" in payload:
        return []

    findings: list[Finding] = []
    text = payload.decode("utf-8", errors="ignore")
    for line_number, line in enumerate(text.splitlines(), 1):
        for label, pattern in PRIVATE_HOME_PATTERNS:
            if pattern.search(line):
                findings.append(Finding(path=path, line=line_number, label=label))
    return findings


def scan_added_lines(base_ref: str, files: list[Path]) -> list[Finding]:
    """Check newly added lines against an existing commit, without historical debt."""

    # Resolve first, even for an empty diff; missing/zero bases must not pass.
    base = subprocess.check_output(
        ["git", "rev-parse", "--verify", "--end-of-options", base_ref + "^{commit}"],
        text=True,
    ).strip()
    changed = subprocess.check_output(
        ["git", "diff", "--name-only", "--no-renames", "--diff-filter=AM",
         "-z", base, "HEAD", "--"],
    )
    changed_paths = {Path(raw.decode("utf-8")) for raw in changed.split(b"\0") if raw}
    findings: list[Finding] = []
    for path in files:
        if path not in changed_paths:
            continue
        diff = subprocess.check_output(
            ["git", "diff", "--no-ext-diff", "--no-textconv", "--no-renames",
             "--unified=0", base, "HEAD", "--", str(path)],
        ).decode("utf-8", errors="ignore")
        line_number = None
        for line in diff.splitlines():
            hunk = re.match(r"^@@ -[0-9]+(?:,[0-9]+)? \+([0-9]+)(?:,[0-9]+)? @@", line)
            if hunk:
                line_number = int(hunk.group(1))
            elif line_number is not None and line.startswith("+"):
                for label, pattern in PRIVATE_HOME_PATTERNS:
                    if pattern.search(line[1:]):
                        findings.append(Finding(path, line_number, label))
                line_number += 1
            elif line_number is not None and line.startswith(" "):
                line_number += 1
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", help="scan added lines from this commit to HEAD")
    args = parser.parse_args()
    try:
        files = list_tracked_files()
        findings = (
            scan_added_lines(args.base_ref, files) if args.base_ref is not None
            else [finding for path in files for finding in scan_file(path)]
        )
    except subprocess.CalledProcessError as exc:
        print(f"publication hygiene could not inspect Git history: {exc}", file=sys.stderr)
        return 1
    if findings:
        for finding in findings:
            print(f"{finding.path}:{finding.line}: {finding.label}")
        print(f"publication hygiene failed: {len(findings)} private path(s)")
        return 1

    scope = "added lines" if args.base_ref is not None else f"{len(files)} tracked files"
    print(f"publication hygiene passed: {scope} scanned")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
