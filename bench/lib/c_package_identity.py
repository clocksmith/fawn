"""Verify retained C package contents against provider and committed sources."""

from __future__ import annotations

import hashlib
from pathlib import PurePosixPath
import re
from typing import Any, Callable


SHA256 = re.compile(r"[0-9a-f]{64}\Z")
GIT_OBJECT = re.compile(r"[0-9a-f]{40}\Z")


def c_manifest_failures(
    manifest: dict[str, Any], provider: dict[str, Any], library_name: str,
    artifact: Callable[[str], bytes | None], source: Callable[[str], bytes | None],
    tree_for_commit: Callable[[str], str | None],
) -> list[str]:
    """Check exact C bytes and provenance without interpreting GPU success."""
    failures = []
    if provider.get("libraryFlavor") != "doe-c" or provider.get("buildMetadataSource") != "c-package":
        failures.append("C provider requires c-package metadata")
    identity = provider.get("buildIdentity")
    identity = identity if isinstance(identity, dict) else {}
    commit, tree = manifest.get("source_commit"), manifest.get("source_tree")
    if (not isinstance(commit, str) or not GIT_OBJECT.fullmatch(commit)
            or not isinstance(tree, str) or not GIT_OBJECT.fullmatch(tree)
            or identity.get("sourceCommit") != commit or identity.get("sourceTree") != tree
            or tree_for_commit(commit) != tree):
        failures.append("C committed source identity differs or is unavailable")
    sources = manifest.get("source_inputs")
    if not isinstance(sources, dict) or not sources:
        failures.append("C manifest source input hashes missing")
    else:
        for name, expected in sources.items():
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or not isinstance(expected, str) or not SHA256.fullmatch(expected):
                failures.append("invalid C source input hash/path")
                continue
            payload = source(name)
            if payload is None or hashlib.sha256(payload).hexdigest() != expected:
                failures.append(f"C source input differs from candidate commit: {name}")
    directory = "bin" if library_name.endswith(".dll") else "lib"
    extension = PurePosixPath(library_name).suffix
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        return failures + ["C manifest artifacts missing"]
    runtime = {name: expected for name, expected in artifacts.items()
               if name.startswith(directory + "/") and PurePosixPath(name).suffix == extension}
    if directory + "/" + library_name not in runtime:
        failures.append("C native library is absent from manifest")
    if not any(PurePosixPath(name).name in ("libdoe_compiler" + extension, "doe_compiler" + extension)
               for name in runtime):
        failures.append("C compiler dependency is absent from manifest")
    for name, expected in runtime.items():
        path = PurePosixPath(name)
        if len(path.parts) != 2 or not isinstance(expected, str) or not SHA256.fullmatch(expected):
            failures.append("invalid C runtime artifact entry")
            continue
        payload = artifact(path.name)
        if payload is None or hashlib.sha256(payload).hexdigest() != expected:
            failures.append(f"C runtime artifact hash differs: {path.name}")
        if path.name == library_name and identity.get("artifactSha256") != expected:
            failures.append("C provider artifact identity differs from manifest")
    return failures
