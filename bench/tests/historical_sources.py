"""Read immutable source fixtures at their reviewed Git revision."""
from __future__ import annotations

from collections.abc import Iterable, Iterator
import io
from pathlib import Path
import subprocess
import unittest


def historical_files(repo: Path, revision: str, paths: Iterable[str]) -> Iterator[tuple[str, bytes]]:
    git = ["git", "-C", str(repo)]
    available = subprocess.run([*git, "cat-file", "-e", f"{revision}^{{commit}}"], capture_output=True)
    if available.returncode and subprocess.check_output(
        [*git, "rev-parse", "--is-shallow-repository"], text=True
    ).strip() == "true":
        raise unittest.SkipTest(f"historical source {revision} unavailable in this shallow checkout")
    available.check_returncode()
    names = sorted(set(paths))
    for name in names:
        if Path(name).is_absolute() or ".." in Path(name).parts or "\n" in name:
            raise ValueError(f"invalid historical source path: {name}")
    request = "".join(f"{revision}:{name}\n" for name in names).encode()
    blobs = io.BytesIO(subprocess.check_output([*git, "cat-file", "--batch"], input=request))
    for name in names:
        header = blobs.readline().split()
        if len(header) != 3 or header[1] != b"blob":
            raise ValueError(f"missing historical source: {revision}:{name}")
        data = blobs.read(int(header[2]))
        if blobs.read(1) != b"\n":
            raise ValueError(f"incomplete historical source: {revision}:{name}")
        yield name, data


def stage_historical_files(root: Path, repo: Path, revision: str, paths: Iterable[str]) -> None:
    for name, data in historical_files(repo, revision, paths):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
