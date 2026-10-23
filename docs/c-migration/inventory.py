#!/usr/bin/env python3
"""Inventory retained Zig at one Git revision; never rewrite source or policy."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import fnmatch
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = "665475907494d2095a760596218b4c2d363cb436"
OUTPUT = Path(__file__).with_name("zig-inventory.json")
LAYOUT = "runtime/zig/source-layout.json"
TESTS = "config/zig-test-inventory.json"
PREFIX = "runtime/zig/"


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def inventory(revision: str) -> dict:
    revision = git("rev-parse", f"{revision}^{{commit}}").decode().strip()
    tree = {}
    for record in git("ls-tree", "-rz", revision).split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        mode, kind, blob = meta.decode().split()
        if kind == "blob":
            tree[path.decode()] = {"mode": mode, "blob": blob}
    sources = sorted(path for path in tree if path.endswith(".zig"))
    cache = {}

    def read(path: str) -> bytes:
        if path not in cache:
            cache[path] = git("cat-file", "blob", tree[path]["blob"])
        return cache[path]

    layout = json.loads(read(LAYOUT))
    catalog = json.loads(read(TESTS))
    layers = layout["architecture"]["layers"]
    generated = layout["architecture"]["generatedSourceContracts"]
    suite_entries = defaultdict(list)
    suite_outputs = set()
    for name, suite in catalog["suites"].items():
        suite_outputs.add(PREFIX + suite["output"])
        for index, entry in enumerate(suite["entries"]):
            suite_entries[PREFIX + entry["path"]].append({
                "suite": name,
                "catalog_pointer": f"/suites/{name}/entries/{index}",
                "kind": entry["kind"],
                "platforms": entry.get("platforms"),
            })
    support = {
        PREFIX + row["path"]: f"/supportFiles/{index}"
        for index, row in enumerate(catalog["supportFiles"])
    }
    charters = {path for path in tree if PurePosixPath(path).name == "CATSCAN.md"}
    import_re = re.compile(r'@import\("([^"\n]+\.zig)"\)')
    test_re = re.compile(r'^\s*test\s+(?:"(?:[^"\\]|\\.)*"\s*)?\{', re.MULTILINE)
    rows = []
    for path in sources:
        content = read(path)
        text = content.decode("utf-8")
        local = path[len(PREFIX):] if path.startswith(PREFIX) else None
        parts = PurePosixPath(path).parts
        ancestry = ["CATSCAN.md"]
        ancestry += ["/".join(parts[:i]) + "/CATSCAN.md" for i in range(1, len(parts))]
        ancestry = [item for item in ancestry if item in charters]
        layer_names = []
        special_roles = []
        owner = str(PurePosixPath(ancestry[-1]).parent)
        owner_basis = f"{ancestry[-1]}: Scope"
        inference = "Nearest inherited charter scope; not a new ownership declaration."
        retained = path.startswith(("bench/out/", "reports/"))
        if local and local.startswith("src/"):
            name = local.split("/")[1]
            if name in layout["topLevelOwners"]:
                owner = PREFIX + "src/" + name
                owner_basis = f"{LAYOUT}#/topLevelOwners/{name}"
                inference = "Owner is declared by the existing source-layout policy."
            else:
                assert local == layout["moduleRoot"], path
                owner = PREFIX + "src"
                owner_basis = f"{LAYOUT}#/moduleRoot"
                inference = "Package root is declared by the existing source-layout policy."
            layer_names = [name for name, spec in layers.items()
                           if any(fnmatch.fnmatchcase(local, glob) for glob in spec["globs"])]
            assert len(layer_names) == 1, (path, layer_names)
            special_roles = [name for name, spec in layout["architecture"]["specialRoles"].items()
                             if any(fnmatch.fnmatchcase(local, glob) for glob in spec["globs"])]
        generated_pointer = local.replace("~", "~0").replace("/", "~1") if local else None
        if retained:
            role, functional_role = "other", "retained-evidence"
            role_basis = "Inferred from retained bench/out or reports path; historical bytes are not active migration work."
        elif local in generated or path in suite_outputs:
            role = "generated"
            functional_role = "tests" if path in suite_outputs else "production"
            role_basis = (f"{TESTS}#/suites: generated output" if path in suite_outputs
                          else f"{LAYOUT}#/architecture/generatedSourceContracts/{generated_pointer}")
        elif local and local.startswith(("tests/", "test_suite")):
            role, functional_role = "tests", "tests"
            role_basis = "Inferred from runtime test directory or suite root; suite membership is separately catalog-bound."
        elif local == "build.zig":
            role, functional_role = "build", "build"
            role_basis = "Canonical Zig build root."
        elif local and local.startswith("src/"):
            role, functional_role = "production", "production"
            role_basis = "Handwritten source-layout implementation source; not a claim of shipped reachability, support or execution."
        elif path == "runtime/bridge/dawn-proc-table/adapter.zig":
            role, functional_role = "production", "production"
            role_basis = "Existing bridge charter assigns native proc-table adapter implementation."
        else:
            role, functional_role = "other", "repository-tooling"
            role_basis = "Inferred repository-only benchmark/tool source, outside active implementation src and test roots."
        imports = []
        for match in import_re.finditer(text):
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), match.group(1)))
            if resolved in tree:
                imports.append(resolved)
        provenance = ["LICENSE", "docs/licensing.md"]
        if local in generated:
            provenance += (["runtime/zig/vendor/webgpu-headers/LICENSE", "config/webgpu-abi-source.json"]
                           if "webgpu_upstream" in local else
                           ["runtime/zig/vendor/spirv-headers/LICENSE", "runtime/zig/vendor/spirv-headers/README.md"])
        rows.append({
            "path": path,
            "git_blob": tree[path]["blob"],
            "sha256": sha256(content),
            "byte_count": len(content),
            "line_count": len(content.splitlines()),
            "role": role,
            "functional_role": functional_role,
            "role_basis": role_basis,
            "owner_path": owner,
            "owner_basis": owner_basis,
            "owner_inference": inference,
            "charter_chain": ancestry,
            "architecture_layers": layer_names,
            "special_roles": special_roles,
            "lexical_test_declaration_count": len(test_re.findall(text)),
            "test_catalog_membership": suite_entries[path],
            "test_catalog_support_pointer": support.get(path),
            "literal_imports_resolved_in_snapshot": sorted(set(imports)),
            "provenance_paths": [p for p in provenance if p in tree],
            "migration_status": "retain_historical_evidence" if retained else "not_migrated",
            "accepted_c_replacement": None,
        })
    totals = {}
    for role in ("production", "tests", "build", "generated", "other"):
        members = [row for row in rows if row["role"] == role]
        totals[role] = {
            "files": len(members),
            "bytes": sum(row["byte_count"] for row in members),
            "lines": sum(row["line_count"] for row in members),
        }
    authorities = ["AGENTS.md", "GOALS.md", "INTENT.md", "CATSCAN.md", LAYOUT, TESTS,
                   "runtime/zig/README.md", "runtime/zig/STYLE.md", "docs/architecture.md",
                   "docs/shader-compiler-architecture.md", "docs/component-charters.md",
                   "docs/process.md", "docs/licensing.md", "LICENSE"]
    fixtures = ["bench/inference-pipeline/kernels/residual.wgsl",
                "bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl",
                "bench/fixtures/wgsl-corpus/game/particle-integrate.wgsl"]
    return {
        "schema_version": 1,
        "purpose": "Source-bound migration inventory, not architectural policy or qualification evidence.",
        "source_revision": revision,
        "source_tree": git("rev-parse", f"{revision}^{{tree}}").decode().strip(),
        "scope": "Every tracked .zig blob at source_revision, including retained historical snapshots; no untracked files, submodule contents or worktree modifications.",
        "status": "Zero production Zig paths have an accepted C replacement in this initial baseline inventory.",
        "accepted_production_paths_migrated": 0,
        "method": {
            "identity": "SHA-256 of exact Git blob bytes; git_blob is the repository object ID.",
            "lines": "Python bytes.splitlines() count, including a final non-newline-terminated line.",
            "ownership": "Existing source-layout topLevelOwners and architecture.layers; nearest inherited charter elsewhere. Role inferences are explicit per row.",
            "tests": "Catalog membership copied by reference from config/zig-test-inventory.json. Lexical test declaration counts are inspection aids, not AST counts or executed-test counts; platform conditions and suite includes still apply.",
            "imports": "Only literal .zig @import targets resolved relative to their file and present in this revision. Not a complete build/dynamic consumer graph or dead-code proof.",
            "roles": "Exclusive primary categories. Generated test suites and generated production definitions have separate functional_role. Production means implementation source, not shipped capability; retained source evidence stays other.",
            "migration": "not_migrated is a baseline acceptance state, not a claim about parallel experimental C code. Historical snapshots remain immutable evidence and are not translation targets.",
        },
        "authority_sources": [{"path": p, "sha256": sha256(read(p))} for p in authorities],
        "licensing": {
            "top_level_license": "LICENSE",
            "identified_text": "MIT License; Copyright (c) 2026 Doe contributors",
            "status": "Present at the inventoried revision, not absent or inferred from README. Preserve existing notices; independent third-party/generated provenance remains separately governed.",
            "third_party_owner": "docs/licensing.md",
        },
        "summary": {
            "files": len(rows),
            "bytes": sum(row["byte_count"] for row in rows),
            "lines": sum(row["line_count"] for row in rows),
            "by_role": totals,
            "by_owner_file_count": dict(sorted(Counter(row["owner_path"] for row in rows).items())),
            "production_use_files_including_generated": sum(row["functional_role"] == "production" for row in rows),
            "historical_evidence_files": sum(row["migration_status"] == "retain_historical_evidence" for row in rows),
            "lexical_test_declarations": sum(row["lexical_test_declaration_count"] for row in rows),
        },
        "representative_wgsl_fixtures": [{"path": p, "sha256": sha256(read(p)),
                                          "byte_count": len(read(p))} for p in fixtures],
        "files": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", default=BASE)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = inventory(args.source_revision)
    rendered = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text() != rendered:
            print("FAIL: inventory differs from the specified immutable Git snapshot")
            return 1
        print(f"PASS: {len(result['files'])} exact tracked Zig blobs, hashes, roles and ownership references")
    else:
        OUTPUT.write_text(rendered)
        print(json.dumps(result["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
