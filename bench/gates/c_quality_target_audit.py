#!/usr/bin/env python3
"""Executable audit gate for docs/c-migration/QUALITY_TARGET.md and quality-counting-contract.json."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_ROOT = REPO_ROOT / "bench"
for _path_entry in (str(REPO_ROOT), str(BENCH_ROOT)):
    if _path_entry not in sys.path:
        sys.path.insert(0, _path_entry)

from bench.lib.bench_utils import detect_repo_root  # noqa: E402

RUNTIME_C_PRODUCTION_SUFFIXES = (
    ".c",
    ".cc",
    ".cpp",
    ".m",
    ".mm",
    ".h",
    ".inc",
    ".map",
    ".mk",
    ".cmd",
    ".rc",
    ".manifest",
)
RUNTIME_C_TOOL_ONLY_PREFIXES = (
    "test_",
    "check_",
    "diagnose_",
    "verify_",
)
RUNTIME_C_TOOL_ONLY_EXACT = frozenset(
    {
        "runtime/c/compiler/tools/tsir_bootstrap_manifest_inputs.c",
        "runtime/c/compiler/tools/tsir_bootstrap_oracle.c",
        "runtime/c/runtime-cli/tools/build_windows_dawn.py",
        "runtime/c/runtime-cli/tools/sdk_fxc.py",
        "runtime/c/tools/mutable_globals.txt",
    }
)
RUNTIME_C_PRODUCTION_FILENAMES = (
    "Makefile",
    "EXPORTED_SYMBOLS.txt",
    "EXTENSION_SYMBOLS.txt",
    "LEGACY_SYMBOLS.txt",
    "SHARED_MEMORY_SYMBOLS.txt",
)
RUNTIME_C_PRODUCTION_EXACT = frozenset(
    {
        "runtime/c/tools/build-native-package.py",
    }
)
BRIDGE_AND_CONSUMER_GLUE_PREFIXES = (
    "runtime/bridge/webgpu-addon/",
    "browser/chromium/launcher/",
    "browser/chromium/patches/",
)
BRIDGE_AND_CONSUMER_GLUE_EXACT = frozenset(
    {
        "runtime/bridge/onnxruntime-ep/Makefile",
        "packages/doe-gpu/binding.gyp",
        "packages/doe-gpu/scripts/build-addon.js",
        "packages/doe-gpu/scripts/build-browser-compiler.js",
        "packages/doe-gpu/scripts/c-native-stage.js",
        "packages/doe-gpu/scripts/native-abi-layout.js",
        "packages/doe-gpu/scripts/platform-staging.js",
        "packages/doe-gpu/scripts/rebuild-native.js",
        "packages/doe-gpu/scripts/stage-platform-package.js",
        "packages/doe-gpu/scripts/stage-prebuilds.js",
        "browser/chromium/scripts/lane-paths.sh",
        "browser/chromium/scripts/patch-chromium-app-doe.sh",
        "browser/chromium/scripts/patch-linux-browser-doe.py",
        "browser/chromium/scripts/refresh-doe-app.sh",
    }
)
PINNED_DEPENDENCY_PATHS = (
    "runtime/vendor/webgpu-headers/webgpu.h",
    "runtime/vendor/webgpu-headers/LICENSE",
    "runtime/vendor/spirv-headers/LICENSE",
    "runtime/bridge/onnxruntime-ep/vendor/onnxruntime/include/onnxruntime_c_api.h",
    "runtime/bridge/onnxruntime-ep/vendor/onnxruntime/include/onnxruntime_ep_c_api.h",
    "runtime/bridge/onnxruntime-ep/vendor/onnxruntime/LICENSE",
)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default="",
        help="Repository root. Auto-detected when omitted.",
    )
    parser.add_argument(
        "--contract",
        default="docs/c-migration/quality-counting-contract.json",
        help="Path to counting contract relative to --root.",
    )
    parser.add_argument(
        "--test-spans",
        default="",
        help="Optional path to test-span artifact (defaults to docs/c-migration/test-span-audit.json when present).",
    )
    parser.add_argument(
        "--no-test-spans",
        action="store_true",
        help="Ignore any default test-span artifact and evaluate unresolved state.",
    )
    parser.add_argument(
        "--stage-dir",
        default="",
        help="Optional staged native C package directory for installed byte accounting.",
    )
    parser.add_argument(
        "--json-out",
        default="",
        help="Optional file path to write the full JSON audit report.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        dest="emit_json",
        help="Emit the JSON audit report to stdout.",
    )
    parser.add_argument(
        "--require-achieved",
        action="store_true",
        help="Exit non-zero unless every 50%% target claim condition is met.",
    )
    return parser.parse_args(argv)


def count_physical_lines(data: bytes) -> int:
    """Exact contract line counter: len(data.splitlines()) on raw bytes."""
    return len(data.splitlines())


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def physical_line_byte_intervals(data: bytes) -> list[tuple[int, int]]:
    """Return half-open [start, end) byte intervals for each physical line in data."""
    if not data:
        return []
    lines = data.splitlines(keepends=True)
    intervals: list[tuple[int, int]] = []
    offset = 0
    for chunk in lines:
        end = offset + len(chunk)
        intervals.append((offset, end))
        offset = end
    return intervals


def evaluate_file_test_spans(
    data: bytes,
    spans: list[dict[str, Any]],
) -> tuple[int, int, list[str]]:
    """Partition physical lines into (implementation_lines, excluded_test_only_lines, errors).

    Only physical lines wholly inside reviewed exclusively_test_only [start, end) byte
    intervals leave implementation; mixed or shared lines remain implementation-charged.
    """
    errors: list[str] = []
    size = len(data)
    exclusive_intervals: list[tuple[int, int]] = []
    for idx, entry in enumerate(spans):
        start = entry.get("start_byte")
        end = entry.get("end_byte")
        purpose = entry.get("purpose")
        suite = entry.get("suite")
        reviewed = entry.get("reviewed")
        if (
            isinstance(start, bool)
            or isinstance(end, bool)
            or not isinstance(start, int)
            or not isinstance(end, int)
            or start < 0
            or end < start
            or end > size
        ):
            errors.append(f"span[{idx}]: invalid half-open byte interval [{start}, {end}) for {size} bytes")
            continue
        if not isinstance(purpose, str) or not purpose.strip():
            errors.append(f"span[{idx}]: missing purpose")
            continue
        if not isinstance(suite, str) or not suite.strip():
            errors.append(f"span[{idx}]: missing suite")
            continue
        if reviewed is not True:
            errors.append(f"span[{idx}]: reviewed must be true")
            continue
        if purpose == "exclusively_test_only" and end > start:
            exclusive_intervals.append((start, end))

    exclusive_intervals.sort()
    merged: list[tuple[int, int]] = []
    for start, end in exclusive_intervals:
        if merged and start <= merged[-1][1]:
            prev_start, prev_end = merged[-1]
            merged[-1] = (prev_start, max(prev_end, end))
        else:
            merged.append((start, end))

    line_intervals = physical_line_byte_intervals(data)
    excluded = 0
    for line_start, line_end in line_intervals:
        if any(span_start <= line_start and line_end <= span_end for span_start, span_end in merged):
            excluded += 1
    total = count_physical_lines(data)
    implementation = total - excluded
    if implementation + excluded != total:
        errors.append("reconciliation mismatch between implementation and excluded test lines")
    return implementation, excluded, errors


def git_tracked_paths(root: Path, prefixes: tuple[str, ...]) -> list[str]:
    output = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z", "--", *prefixes],
    )
    return sorted({entry.decode("utf-8") for entry in output.split(b"\0") if entry})


def git_identity(root: Path) -> tuple[str, str, dict[str, str]]:
    commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--verify", "HEAD^{commit}"],
        text=True,
    ).strip()
    tree = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--verify", "HEAD^{tree}"],
        text=True,
    ).strip()
    status_raw = subprocess.check_output(
        ["git", "-C", str(root), "status", "--porcelain=v1", "-z"],
    )
    dirty: dict[str, str] = {}
    entries = status_raw.split(b"\0")
    idx = 0
    while idx < len(entries):
        record = entries[idx]
        idx += 1
        if not record:
            continue
        code = record[:2].decode("utf-8", errors="replace")
        rel_path = record[3:].decode("utf-8", errors="replace")
        if "R" in code or "C" in code:
            idx += 1
        target = root / rel_path
        if target.is_symlink():
            dirty[rel_path] = "symlink"
        elif target.is_file():
            dirty[rel_path] = sha256_bytes(target.read_bytes())
        else:
            dirty[rel_path] = f"missing_or_directory:{code.strip()}"
    return commit, tree, dirty


def runtime_c_owner(rel_path: str) -> str:
    parts = rel_path.split("/")
    if len(parts) >= 3 and parts[0] == "runtime" and parts[1] == "c":
        if parts[2] == "backends" and len(parts) >= 4:
            return f"runtime/c/backends/{parts[3]}"
        return f"runtime/c/{parts[2]}"
    if rel_path.startswith("runtime/bridge/"):
        return "/".join(parts[:3])
    if rel_path.startswith("packages/doe-gpu/"):
        return "packages/doe-gpu"
    if rel_path.startswith("browser/chromium/"):
        return "browser/chromium"
    return parts[0]


def is_runtime_c_test_or_tool_path(rel_path: str) -> bool:
    if rel_path in RUNTIME_C_TOOL_ONLY_EXACT:
        return True
    parts = rel_path.split("/")
    if any(
        seg in ("tests", "independent_tests") or seg.startswith("independent_tests_")
        for seg in parts
    ):
        return True
    name = parts[-1]
    if "/tools/" in rel_path and name.startswith(RUNTIME_C_TOOL_ONLY_PREFIXES):
        return True
    return False


def is_charged_production_path(rel_path: str) -> bool:
    if rel_path in RUNTIME_C_PRODUCTION_EXACT or rel_path in BRIDGE_AND_CONSUMER_GLUE_EXACT:
        return True
    if rel_path.startswith("runtime/c/"):
        if is_runtime_c_test_or_tool_path(rel_path):
            return False
        name = rel_path.rsplit("/", 1)[-1]
        if name in RUNTIME_C_PRODUCTION_FILENAMES:
            return True
        if "/tools/" in rel_path and name.endswith(".py"):
            return True
        return any(name.endswith(suf) for suf in RUNTIME_C_PRODUCTION_SUFFIXES)
    if rel_path.startswith("runtime/bridge/onnxruntime-ep/src/"):
        name = rel_path.rsplit("/", 1)[-1]
        if "smoke" in name:
            return False
        return any(name.endswith(suf) for suf in (".c", ".cc", ".cpp", ".h"))
    if any(rel_path.startswith(prefix) for prefix in BRIDGE_AND_CONSUMER_GLUE_PREFIXES):
        name = rel_path.rsplit("/", 1)[-1]
        if name.endswith(".md") or name == ".gitignore":
            return False
        return any(
            name.endswith(suf)
            for suf in (".c", ".cc", ".cpp", ".h", ".rc", ".manifest", ".patch")
        )
    return False


def is_test_or_tooling_view_path(rel_path: str) -> bool:
    if rel_path.startswith("runtime/c/"):
        return is_runtime_c_test_or_tool_path(rel_path)
    if rel_path.startswith("runtime/bridge/onnxruntime-ep/src/"):
        return "smoke" in rel_path.rsplit("/", 1)[-1]
    if rel_path.startswith("packages/doe-gpu/test/") or rel_path.startswith("browser/chromium/tests/"):
        return True
    return False


def is_retained_reference_or_receipt_path(rel_path: str) -> bool:
    if not rel_path.startswith("runtime/c/"):
        return False
    if is_charged_production_path(rel_path) or is_runtime_c_test_or_tool_path(rel_path):
        return False
    return True


def summarize_file_set(
    root: Path,
    rel_paths: list[str],
    integrity_errors: list[str],
) -> tuple[dict[str, Any], dict[str, tuple[int, int, str]]]:
    total_bytes = 0
    total_lines = 0
    by_owner: dict[str, dict[str, int]] = {}
    records: dict[str, tuple[int, int, str]] = {}
    aggregate = hashlib.sha256()

    for rel_path in sorted(set(rel_paths)):
        full = root / rel_path
        if full.is_symlink():
            integrity_errors.append(f"{rel_path}: source symlink is forbidden in size accounting")
            continue
        if not full.is_file():
            integrity_errors.append(f"{rel_path}: missing regular file")
            continue
        data = full.read_bytes()
        lines = count_physical_lines(data)
        size = len(data)
        digest = sha256_bytes(data)
        records[rel_path] = (size, lines, digest)
        total_bytes += size
        total_lines += lines
        owner = runtime_c_owner(rel_path)
        bucket = by_owner.setdefault(owner, {"files": 0, "bytes": 0, "physical_lines": 0})
        bucket["files"] += 1
        bucket["bytes"] += size
        bucket["physical_lines"] += lines
        aggregate.update(rel_path.encode("utf-8") + b"\0" + digest.encode("ascii") + b"\n")

    return (
        {
            "files": len(records),
            "bytes": total_bytes,
            "physical_lines": total_lines,
            "closure_sha256": aggregate.hexdigest(),
            "by_owner": dict(sorted(by_owner.items())),
        },
        records,
    )


def validate_baseline(
    root: Path,
    contract: dict[str, Any],
) -> tuple[bool, list[str], dict[str, Any], dict[str, Any]]:
    errors: list[str] = []
    baseline_cfg = contract.get("baseline")
    if not isinstance(baseline_cfg, dict):
        return False, ["contract missing baseline object"], {}, {}

    inv_rel = baseline_cfg.get("inventory_path", "")
    inv_path = root / inv_rel
    if not inv_rel or not inv_path.is_file() or inv_path.is_symlink():
        return False, [f"baseline inventory missing or symlink: {inv_rel}"], {}, {}

    raw = inv_path.read_bytes()
    actual_sha = sha256_bytes(raw)
    expected_sha = baseline_cfg.get("inventory_sha256")
    if actual_sha != expected_sha:
        errors.append(
            f"baseline inventory sha256 mismatch: expected {expected_sha}, got {actual_sha}"
        )

    try:
        inventory = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return False, [*errors, f"invalid baseline inventory JSON: {exc}"], {}, {}

    if inventory.get("source_revision") != baseline_cfg.get("source_revision"):
        errors.append("baseline source_revision mismatch")
    if inventory.get("source_tree") != baseline_cfg.get("source_tree"):
        errors.append("baseline source_tree mismatch")

    files = inventory.get("files", [])
    if not isinstance(files, list):
        return False, [*errors, "baseline files field must be a list"], {}, {}

    total_files = len(files)
    total_lines = sum(int(f.get("line_count", 0)) for f in files)
    prod_files = [f for f in files if f.get("role") == baseline_cfg.get("headline_role", "production")]
    prod_file_count = len(prod_files)
    prod_lines = sum(int(f.get("line_count", 0)) for f in prod_files)
    prod_test_decls = sum(int(f.get("lexical_test_declaration_count", 0)) for f in prod_files)

    if total_files != baseline_cfg.get("inventory_files"):
        errors.append(f"baseline inventory_files mismatch: {total_files}")
    if total_lines != baseline_cfg.get("inventory_physical_lines"):
        errors.append(f"baseline inventory_physical_lines mismatch: {total_lines}")
    if prod_file_count != baseline_cfg.get("headline_files"):
        errors.append(f"baseline headline_files mismatch: {prod_file_count}")
    if prod_lines != baseline_cfg.get("headline_physical_lines"):
        errors.append(f"baseline headline_physical_lines mismatch: {prod_lines}")
    if prod_test_decls != baseline_cfg.get("production_lexical_test_declarations"):
        errors.append(f"baseline production_lexical_test_declarations mismatch: {prod_test_decls}")

    summary = {
        "inventory_path": inv_rel,
        "inventory_sha256": actual_sha,
        "source_revision": inventory.get("source_revision"),
        "source_tree": inventory.get("source_tree"),
        "inventory_files": total_files,
        "inventory_physical_lines": total_lines,
        "headline_files": prod_file_count,
        "headline_physical_lines": prod_lines,
        "production_lexical_test_declarations": prod_test_decls,
    }
    return len(errors) == 0, errors, summary, inventory


def evaluate_stage_bytes(stage_dir: Path | None, root: Path) -> dict[str, Any]:
    tracked_tarballs: list[dict[str, Any]] = []
    for tgz in sorted((root / "packages" / "doe-gpu").glob("*.tgz")):
        if tgz.is_file() and not tgz.is_symlink():
            data = tgz.read_bytes()
            tracked_tarballs.append(
                {
                    "path": tgz.relative_to(root).as_posix(),
                    "bytes": len(data),
                    "sha256": sha256_bytes(data),
                }
            )
    stage_summary: dict[str, Any] | None = None
    if stage_dir is not None:
        files = [p for p in sorted(stage_dir.rglob("*")) if p.is_file() and not p.is_symlink()]
        total_bytes = 0
        by_basename: dict[str, list[dict[str, Any]]] = {}
        for item in files:
            rel = item.relative_to(stage_dir).as_posix()
            size = item.stat().st_size
            total_bytes += size
            by_basename.setdefault(item.name, []).append({"path": rel, "bytes": size})
        duplicates = {
            name: copies for name, copies in sorted(by_basename.items()) if len(copies) > 1
        }
        stage_summary = {
            "stage_dir": str(stage_dir),
            "files": len(files),
            "total_bytes": total_bytes,
            "manifest_present": (stage_dir / "MANIFEST.json").is_file(),
            "duplicate_basenames": duplicates,
        }
    return {
        "tracked_package_archives": tracked_tarballs,
        "staged_native_package": stage_summary,
    }


def run_audit(
    root: Path,
    contract_rel: str = "docs/c-migration/quality-counting-contract.json",
    test_spans_path: Path | None = None,
    stage_dir: Path | None = None,
) -> dict[str, Any]:
    integrity_errors: list[str] = []
    contract_path = root / contract_rel
    if not contract_path.is_file() or contract_path.is_symlink():
        return {
            "integrity_ok": False,
            "integrity_errors": [f"missing or symlink counting contract: {contract_rel}"],
            "achieved": False,
        }
    contract_bytes = contract_path.read_bytes()
    contract_sha = sha256_bytes(contract_bytes)
    contract = json.loads(contract_bytes.decode("utf-8"))

    baseline_ok, baseline_errors, baseline_summary, inventory = validate_baseline(root, contract)
    integrity_errors.extend(baseline_errors)

    tracked = git_tracked_paths(
        root,
        ("runtime/c", "runtime/zig", "runtime/bridge", "packages/doe-gpu", "browser/chromium"),
    )
    charged_paths = [p for p in tracked if is_charged_production_path(p)]
    test_tool_paths = [p for p in tracked if is_test_or_tooling_view_path(p)]
    retained_c_doc_paths = [p for p in tracked if is_retained_reference_or_receipt_path(p)]
    active_zig_paths = [
        p
        for p in tracked
        if (p.startswith("runtime/zig/") or p == "runtime/bridge/dawn-proc-table/adapter.zig")
        and p.endswith(".zig")
    ]

    runtime_c_all = [p for p in tracked if p.startswith("runtime/c/")]
    runtime_c_partition_union = {
        p
        for p in (*charged_paths, *test_tool_paths, *retained_c_doc_paths)
        if p.startswith("runtime/c/")
    }
    if set(runtime_c_all) != runtime_c_partition_union:
        unaccounted = sorted(set(runtime_c_all) - runtime_c_partition_union)
        integrity_errors.append(f"unaccounted runtime/c paths: {unaccounted[:10]}")

    charged_summary, charged_records = summarize_file_set(root, charged_paths, integrity_errors)
    tests_summary, _ = summarize_file_set(root, test_tool_paths, integrity_errors)
    retained_c_summary, _ = summarize_file_set(root, retained_c_doc_paths, integrity_errors)
    active_zig_summary, _ = summarize_file_set(root, active_zig_paths, integrity_errors)
    deps_summary, _ = summarize_file_set(root, list(PINNED_DEPENDENCY_PATHS), integrity_errors)

    zig_refs_in_charged: list[str] = []
    for rel_path in charged_paths:
        if rel_path.endswith(("Makefile", ".mk", ".py", ".js", ".sh", ".gyp")):
            text = (root / rel_path).read_bytes().decode("utf-8", errors="replace")
            if "zig build" in text or "runtime/zig" in text:
                zig_refs_in_charged.append(rel_path)

    if test_spans_path is None:
        default_spans = root / "docs/c-migration/test-span-audit.json"
        if default_spans.is_file():
            test_spans_path = default_spans
    elif test_spans_path == Path("__NONE__"):
        test_spans_path = None

    classification_sha: str | None = None
    impl_only_view: dict[str, Any] = {
        "status": "unresolved",
        "baseline_implementation_only_lines": None,
        "candidate_implementation_only_lines": None,
        "candidate_excluded_test_only_lines": None,
        "normalized_condition_met": False,
        "reason": "symmetric reviewed half-open byte-interval test-span artifact not supplied",
    }
    if test_spans_path is not None:
        if not test_spans_path.is_file() or test_spans_path.is_symlink():
            integrity_errors.append(f"invalid --test-spans path: {test_spans_path}")
        else:
            span_bytes = test_spans_path.read_bytes()
            classification_sha = sha256_bytes(span_bytes)
            span_doc = json.loads(span_bytes.decode("utf-8"))
            base_map = span_doc.get("baseline_files", {})
            cand_map = span_doc.get("candidate_files", {})
            base_decl = span_doc.get("baseline_implementation_only_lines")
            base_reviewed = span_doc.get("baseline_all_628_files_reviewed") is True
            human_signoff = span_doc.get("method", {}).get("human_readability_and_span_signoff") is True
            span_errors: list[str] = []

            prod_inv = [
                f
                for f in inventory.get("files", [])
                if f.get("role") == contract.get("baseline", {}).get("headline_role", "production")
            ]
            base_impl_total = 0
            base_excl_total = 0
            for entry in prod_inv:
                rel_path = entry["path"]
                record = base_map.get(rel_path)
                if not isinstance(record, dict):
                    span_errors.append(f"missing baseline span record: {rel_path}")
                    continue
                if record.get("sha256") != entry.get("sha256"):
                    span_errors.append(f"baseline sha256 mismatch in span record: {rel_path}")
                    continue
                expected_lines = int(entry.get("line_count", 0))
                impl_l = record.get("implementation_lines")
                excl_l = record.get("excluded_test_only_lines")
                if (
                    not isinstance(impl_l, int)
                    or not isinstance(excl_l, int)
                    or impl_l < 0
                    or excl_l < 0
                    or impl_l + excl_l != expected_lines
                ):
                    span_errors.append(f"baseline line reconciliation mismatch: {rel_path}")
                    continue
                max_bytes = int(entry.get("byte_count", 0))
                for idx, sp in enumerate(record.get("spans", [])):
                    sb = sp.get("start_byte")
                    eb = sp.get("end_byte")
                    if (
                        not isinstance(sb, int)
                        or not isinstance(eb, int)
                        or sb < 0
                        or eb < sb
                        or eb > max_bytes
                        or sp.get("reviewed") is not True
                    ):
                        span_errors.append(f"{rel_path}: invalid baseline span[{idx}]")
                base_impl_total += impl_l
                base_excl_total += excl_l

            if isinstance(base_decl, int) and base_impl_total != base_decl:
                span_errors.append(
                    f"baseline_implementation_only_lines mismatch: {base_impl_total} != {base_decl}"
                )

            cand_impl_total = 0
            cand_excl_total = 0
            for rel_path, (_, _, expected_digest) in sorted(charged_records.items()):
                record = cand_map.get(rel_path)
                if not isinstance(record, dict):
                    span_errors.append(f"missing candidate span record: {rel_path}")
                    continue
                if record.get("sha256") != expected_digest:
                    span_errors.append(f"sha256 drift in span record: {rel_path}")
                    continue
                impl_lines, excl_lines, errs = evaluate_file_test_spans(
                    (root / rel_path).read_bytes(),
                    record.get("spans", []),
                )
                span_errors.extend(f"{rel_path}: {e}" for e in errs)
                if record.get("implementation_lines") not in (None, impl_lines) or record.get(
                    "excluded_test_only_lines"
                ) not in (None, excl_lines):
                    span_errors.append(f"candidate stored line summary drift: {rel_path}")
                cand_impl_total += impl_lines
                cand_excl_total += excl_lines

            if span_errors or not base_reviewed or not isinstance(base_decl, int):
                impl_only_view = {
                    "status": "unresolved",
                    "baseline_implementation_only_lines": base_decl if isinstance(base_decl, int) else None,
                    "candidate_implementation_only_lines": None,
                    "candidate_excluded_test_only_lines": None,
                    "normalized_condition_met": False,
                    "errors": span_errors[:25],
                }
                if span_errors:
                    integrity_errors.extend(span_errors[:10])
            else:
                norm_met = (2 * cand_impl_total) <= base_decl
                impl_only_view = {
                    "status": "resolved",
                    "baseline_implementation_only_lines": base_decl,
                    "baseline_excluded_test_only_lines": base_excl_total,
                    "normalized_50pct_ceiling_lines": base_decl // 2,
                    "candidate_implementation_only_lines": cand_impl_total,
                    "candidate_excluded_test_only_lines": cand_excl_total,
                    "normalized_condition_met": norm_met,
                    "human_readability_and_span_signoff": human_signoff,
                }

    receipt_paths = [
        p
        for p in retained_c_doc_paths
        if p.endswith(".json") and "RECEIPT" in p.rsplit("/", 1)[-1]
    ]
    receipt_summary, _ = summarize_file_set(root, receipt_paths, integrity_errors)
    commit, tree, dirty_files = git_identity(root)

    ceiling = int(contract["claim_conditions_all_required"]["fixed_headline_ceiling_lines"])
    charged_lines = int(charged_summary["physical_lines"])
    headline_met = charged_lines <= ceiling

    unmet: list[str] = []
    if not baseline_ok or integrity_errors:
        unmet.append("identity_or_baseline_integrity_failed")
    if not headline_met:
        unmet.append(
            f"headline_ceiling_exceeded:{charged_lines}>{ceiling}"
        )
    if impl_only_view["status"] != "resolved":
        unmet.append("test_span_audit_unresolved")
    elif not impl_only_view["normalized_condition_met"]:
        twice_cand = 2 * int(impl_only_view["candidate_implementation_only_lines"])
        base_lim = int(impl_only_view["baseline_implementation_only_lines"])
        unmet.append(f"normalized_implementation_condition_unmet:{twice_cand}>{base_lim}")
    if dirty_files:
        unmet.append("working_tree_dirty")
    if zig_refs_in_charged:
        unmet.append("charged_production_references_zig")
    unmet.append("full_migration_and_readability_gates_unestablished")

    overlap_charged_tests = sorted(set(charged_paths) & set(test_tool_paths))
    inv_other = inventory.get("summary", {}).get("by_role", {}).get("other", {})
    toolchains_path = root / "config" / "toolchains.json"
    toolchains_sha = (
        sha256_bytes(toolchains_path.read_bytes()) if toolchains_path.is_file() else None
    )

    return {
        "schema_version": 1,
        "contract_path": contract_rel,
        "integrity_ok": baseline_ok and len(integrity_errors) == 0,
        "integrity_errors": integrity_errors,
        "achieved": len(unmet) == 0,
        "unmet_claim_conditions": unmet,
        "headline_check": {
            "fixed_headline_ceiling_lines": ceiling,
            "charged_production_physical_lines": charged_lines,
            "delta_lines": charged_lines - ceiling,
            "ratio_vs_baseline_headline": round(
                charged_lines / baseline_summary.get("headline_physical_lines", 155898), 4
            ),
            "met": headline_met,
        },
        "report_identity": {
            "candidate_commit_and_tree": {"commit": commit, "tree": tree},
            "dirty_file_hashes_if_any": dirty_files,
            "baseline_inventory_sha256": baseline_summary.get("inventory_sha256"),
            "counting_contract_sha256": contract_sha,
            "classification_artifact_sha256": classification_sha,
            "source_closure_hashes": {
                "charged_production_closure_sha256": charged_summary["closure_sha256"],
                "charged_production_file_count": charged_summary["files"],
            },
            "dependencies": {
                "pinned_unchanged_files": deps_summary["files"],
                "pinned_unchanged_bytes": deps_summary["bytes"],
                "pinned_unchanged_lines": deps_summary["physical_lines"],
                "pinned_unchanged_closure_sha256": deps_summary["closure_sha256"],
            },
            "toolchain_and_flags": {
                "python_version": sys.version.split()[0],
                "toolchains_config_sha256": toolchains_sha,
                "c_file_line_limit": 999,
            },
            "evidence_hashes": {
                "developer_receipt_files": receipt_summary["files"],
                "developer_receipt_closure_sha256": receipt_summary["closure_sha256"],
            },
        },
        "views": {
            "final_charged_production_delivery": charged_summary,
            "baseline_and_final_implementation_only": impl_only_view,
            "tests_and_tooling": {
                **tests_summary,
                "overlap_with_charged_production_paths": overlap_charged_tests,
            },
            "generated_and_dependencies": {
                "production_generated_sources": {
                    "files": 0,
                    "bytes": 0,
                    "physical_lines": 0,
                },
                "pinned_unchanged_dependencies": deps_summary,
            },
            "transitional_c_zig_adapters": {
                "charged_c_and_glue_lines": charged_lines,
                "retained_tracked_zig_files": active_zig_summary["files"],
                "retained_tracked_zig_lines": active_zig_summary["physical_lines"],
                "zig_references_in_charged_production": zig_refs_in_charged,
            },
            "retained_reference_sources": {
                "baseline_historical_zig_role_other": inv_other,
                "runtime_c_docs_contracts_and_receipts": retained_c_summary,
            },
            "installed_package_bytes": evaluate_stage_bytes(stage_dir, root),
        },
        "baseline": baseline_summary,
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = detect_repo_root(args.root) if args.root else REPO_ROOT
    if args.no_test_spans:
        spans_path: Path | None = Path("__NONE__")
    elif args.test_spans:
        cand_span = Path(args.test_spans)
        spans_path = cand_span.resolve() if cand_span.is_absolute() else (root / cand_span).resolve()
    else:
        spans_path = None
    stage_path = Path(args.stage_dir).resolve() if args.stage_dir else None

    report = run_audit(
        root=root,
        contract_rel=args.contract,
        test_spans_path=spans_path,
        stage_dir=stage_path,
    )
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        out_path = Path(args.json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(rendered, encoding="utf-8")
    if args.emit_json:
        sys.stdout.write(rendered)
    else:
        hc = report.get("headline_check", {})
        print(
            "c-quality-target-audit: "
            f"integrity_ok={report.get('integrity_ok')} "
            f"achieved={report.get('achieved')} "
            f"charged_lines={hc.get('charged_production_physical_lines')} "
            f"ceiling={hc.get('fixed_headline_ceiling_lines')} "
            f"blockers={','.join(report.get('unmet_claim_conditions', []))}"
        )

    if not report.get("integrity_ok"):
        for err in report.get("integrity_errors", []):
            print(f"FAIL: {err}", file=sys.stderr)
        return 1
    if args.require_achieved and not report.get("achieved"):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
