"""Build the existing cube inventory from registered evidence, without execution."""

from __future__ import annotations

import argparse
from pathlib import Path
from os.path import relpath
import re
import subprocess
from typing import Any

import jsonschema

from bench.inventory import storage, markdown, dashboard, model, sources
from bench.lib.hash_utils import file_sha256
from bench.lib.bench_utils import load_json_object
from bench.lib.config_validation import load_validated_config, validate_schema


def write_text(path: Path, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")


def build_inventory(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    """Generate one deterministic view over registered, hash-bound cube sources."""
    policy_path = root / args.policy
    policy = load_validated_config(policy_path)["inventory"]
    registration_path = root / args.sources
    registered = load_json_object(registration_path)
    schema = load_json_object(root / "config/benchmark-cube.schema.json")
    jsonschema.Draft202012Validator(schema["$defs"]["sourceRegistration"]).validate(
        registered
    )
    seen_ids: set[str] = set()
    seen_evidence: set[tuple[str, str]] = set()
    family_ids = {f["id"] for f in registered["families"]}
    if any(
        re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", value) is None
        for value in family_ids
    ):
        raise ValueError("Inventory family IDs must be safe file-name identifiers")
    if len(family_ids) != len(registered["families"]):
        raise ValueError("Duplicate inventory family")
    rows = []
    for entry in registered["sources"]:
        if entry["id"] in seen_ids:
            raise ValueError("Duplicate registration ID: " + entry["id"])
        key = (
            str((root / entry["path"]).resolve()) if entry["path"] else None,
            entry["adapter"],
        )
        if entry["path"] is not None and key in seen_evidence:
            raise ValueError("Duplicate evidence registration: " + entry["path"])
        if entry["familyId"] not in family_ids:
            raise ValueError("Unknown registration family: " + entry["familyId"])
        if entry["comparisonType"] not in policy["comparisonTypes"]:
            raise ValueError("Unknown comparison type: " + entry["comparisonType"])
        seen_ids.add(entry["id"])
        seen_evidence.add(key)
        rows.extend(sources.normalize_source(root, entry, policy))
    rows.sort(key=lambda row: row["rowId"])
    if len({row["rowId"] for row in rows}) != len(rows):
        raise ValueError("Duplicate normalized observation identity")
    # Schemas remain versioned: legacy cube rows and inventory rows validate
    # against the same row contract, without changing old consumers' shapes.
    row_schema = root / "config/benchmark-cube-row.schema.json"
    validator = jsonschema.Draft202012Validator(
        load_json_object(row_schema), format_checker=jsonschema.FormatChecker()
    )
    for row in rows:
        validator.validate(row)
    current_source = subprocess.check_output(
        [
            "git",
            "-C",
            str(root),
            "log",
            "-1",
            "--format=%H",
            args.source_ref,
            "--",
            *policy["currentSourcePaths"],
        ],
        text=True,
    ).strip()
    if not current_source:
        current_source = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", args.source_ref], text=True
        ).strip()
    selected = model.select_inventory_current(rows, current_source)
    families = []
    for family in registered["families"]:
        ids = [r["rowId"] for r in rows if r["familyId"] == family["id"]]
        families.append({**family, "observations": ids})
    payload = {
        "schemaVersion": 2,
        "kind": "benchmark-cube-inventory",
        "currentSource": current_source,
        "policy": {
            "path": args.policy,
            "sha256": file_sha256(policy_path),
        },
        "sources": {
            "path": args.sources,
            "sha256": file_sha256(registration_path),
        },
        "families": families,
        "rows": rows,
        "current": selected,
    }
    validate_schema(root / "config/benchmark-cube.schema.json", payload)
    version = policy.get(
        "storageVersion", 2
    )  # Inventory policy v1 wrote expanded rows.
    if version not in (2, 3, 4):
        raise ValueError("Unsupported inventory storage policy")
    stored = storage.compact(payload, version=version) if version in (3, 4) else payload
    validate_schema(root / "config/benchmark-cube.schema.json", stored)
    outputs = {
        root / args.index_out: storage.canonical(stored) + "\n",
        root / args.current_out: markdown.render_current(
            payload,
            index_href=relpath(root / args.index_out, (root / args.current_out).parent),
        ),
    }
    outputs.update(
        markdown.render_families(
            payload, root / args.current_out, root, root / args.index_out
        )
    )
    if getattr(args, "expanded_index_out", ""):
        outputs[root / args.expanded_index_out] = storage.canonical(payload) + "\n"
    if args.dashboard_html_out:
        path = root / args.dashboard_html_out
        outputs[path] = dashboard.render_dashboard(
            payload, path, current_path=root / args.current_out
        )
    if getattr(args, "losses_out", ""):
        outputs[root / args.losses_out] = markdown.render_losses(
            payload,
            report_path=root / args.losses_out,
            current_path=root / args.current_out,
        )
    family_dir = (root / args.current_out).parent / "families"
    orphaned = [path for path in family_dir.glob("*.md") if path not in outputs]
    for path in orphaned:
        if "<!-- Generated by benchmark cube; do not edit. -->" not in path.read_text(
            encoding="utf-8"
        ):
            raise ValueError("Unowned file in generated family directory: " + str(path))
    stale = [
        str(path)
        for path, body in outputs.items()
        if not path.is_file() or path.read_text(encoding="utf-8") != body
    ]
    stale += [str(path) for path in orphaned]
    if args.check and stale:
        raise ValueError("Stale benchmark inventory: " + ", ".join(stale))
    if not args.check:
        for path in orphaned:
            path.unlink()  # Generated projection only; retained source evidence is untouched.
        for path, body in outputs.items():
            write_text(path, body)
    print(
        f"Benchmark cube inventory: {len(rows)} observations; {len(families)} families; "
        f"{len(selected)} current selections" + ("; check passed" if args.check else "")
    )
    return payload
