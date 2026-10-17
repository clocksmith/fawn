#!/usr/bin/env python3
"""Check WGSL corpus materialization receipts."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

try:
    from bench.tools._repo_import import ensure_repo_root
except ModuleNotFoundError:
    from _repo_import import ensure_repo_root

ensure_repo_root(__file__)

from bench.lib.receipt_validation import load_json, failure, is_sha256, safe_repo_path, resolve_path  # noqa: E402
from bench.lib.wgsl_source import normalize_source, normalized_sha256  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--receipt", required=True, help="wgsl_corpus_materialization JSON path.")
    parser.add_argument(
        "--verify-files-root",
        default="",
        help="Resolve relative materialized paths under this root and verify normalized hashes.",
    )
    parser.add_argument("--json", action="store_true", dest="emit_json")
    return parser.parse_args()


def check_receipt(payload: dict[str, Any], verify_files_root: Path | None = None) -> list[dict[str, str]]:
    failures: list[dict[str, str]] = []
    if payload.get("artifactKind") != "wgsl_corpus_materialization":
        failures.append(
            failure("invalid_artifact_kind", "artifactKind", "artifactKind must be wgsl_corpus_materialization")
        )
    if payload.get("materializationStatus") != "pass":
        failure_codes = payload.get("failureCodes")
        if isinstance(failure_codes, list) and failure_codes:
            for index, item in enumerate(failure_codes):
                if isinstance(item, dict):
                    failures.append(
                        failure(
                            str(item.get("code", "materialization_failure")),
                            str(item.get("path", f"failureCodes[{index}]")),
                            str(item.get("message", "WGSL corpus materialization failure")),
                        )
                    )
        else:
            failures.append(failure("materialization_status_not_pass", "materializationStatus", "materializationStatus must be pass"))

    rows = payload.get("rows", [])
    if not isinstance(rows, list) or not rows:
        failures.append(failure("missing_materialized_rows", "rows", "materialized rows must be non-empty"))
        return failures

    seen_shader_ids: set[str] = set()
    seen_paths: set[str] = set()
    for index, row in enumerate(rows):
        row_path = f"rows[{index}]"
        if not isinstance(row, dict):
            failures.append(failure("invalid_materialized_row", row_path, "materialized row must be an object"))
            continue
        shader_id = row.get("shaderId")
        if not isinstance(shader_id, str) or not shader_id:
            failures.append(failure("missing_shader_id", f"{row_path}.shaderId", "shaderId is required"))
        elif shader_id in seen_shader_ids:
            failures.append(failure("duplicate_shader_id", f"{row_path}.shaderId", f"duplicate shaderId {shader_id}"))
        else:
            seen_shader_ids.add(shader_id)
        materialized_path = row.get("materializedPath")
        if not isinstance(materialized_path, str) or not materialized_path:
            failures.append(failure("missing_materialized_path", f"{row_path}.materializedPath", "materializedPath is required"))
        elif materialized_path in seen_paths:
            failures.append(failure("duplicate_materialized_path", f"{row_path}.materializedPath", f"duplicate materializedPath {materialized_path}"))
        else:
            seen_paths.add(materialized_path)
        source_hash = row.get("normalizedSourceSha256")
        if not is_sha256(source_hash):
            failures.append(failure("missing_source_hash", f"{row_path}.normalizedSourceSha256", "source hash must be sha256 hex"))
        for field in ("category", "sourcePath", "expectedValidity"):
            if not row.get(field):
                failures.append(failure("missing_materialized_field", f"{row_path}.{field}", f"{field} is required"))
        source_path = row.get("sourcePath")
        if isinstance(source_path, str) and source_path and not safe_repo_path(source_path):
            failures.append(
                failure(
                    "unsafe_source_path",
                    f"{row_path}.sourcePath",
                    "sourcePath must be repo-relative",
                )
            )
        if not isinstance(row.get("expectedBackendTargets"), list) or not row.get("expectedBackendTargets"):
            failures.append(failure("missing_backend_targets", f"{row_path}.expectedBackendTargets", "backend targets are required"))
        if not isinstance(row.get("shaderStages"), list) or not row.get("shaderStages"):
            failures.append(failure("missing_shader_stages", f"{row_path}.shaderStages", "shader stages are required"))
        if verify_files_root is not None and isinstance(materialized_path, str) and is_sha256(source_hash):
            resolved = resolve_path(materialized_path, verify_files_root)
            if resolved is None:
                failures.append(
                    failure(
                        "unsafe_materialized_path",
                        f"{row_path}.materializedPath",
                        "materializedPath must resolve under verify-files-root",
                    )
                )
                continue
            if not resolved.is_file():
                failures.append(failure("materialized_file_missing", f"{row_path}.materializedPath", f"materialized file not found: {materialized_path}"))
            else:
                actual_hash = normalized_sha256(resolved)
                if actual_hash != source_hash:
                    failures.append(
                        failure(
                            "materialized_hash_mismatch",
                            f"{row_path}.normalizedSourceSha256",
                            f"expected {source_hash}, got {actual_hash}",
                        )
                    )
    return failures


def main() -> int:
    args = parse_args()
    verify_files_root = Path(args.verify_files_root).resolve() if args.verify_files_root else None
    failures = check_receipt(load_json(Path(args.receipt)), verify_files_root)
    report = {
        "schemaVersion": 1,
        "artifactKind": "wgsl_corpus_materialization_check",
        "receiptPath": args.receipt,
        "status": "fail" if failures else "pass",
        "failures": failures,
    }
    if args.emit_json:
        print(json.dumps(report, indent=2))
    elif failures:
        print("FAIL: WGSL corpus materialization")
        for item in failures:
            print(f"- {item['code']}: {item['path']}: {item['message']}")
    else:
        print("PASS: WGSL corpus materialization")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
