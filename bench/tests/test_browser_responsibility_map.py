#!/usr/bin/env python3
"""Tests for the Chromium browser responsibility map contract."""

from __future__ import annotations

import json
import importlib.util
from pathlib import Path

from bench.tools import check_browser_responsibility_map as map_check


REPO_ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = REPO_ROOT / "config" / "browser-responsibility-map.json"
BROWSER_GATE_PATH = REPO_ROOT / "bench" / "browser" / "browser_gate.py"
BROWSER_CLAIM_GATE_PATH = REPO_ROOT / "bench" / "browser" / "browser_claim_gate.py"

CLAIM_BINDING_FIELDS = {
    "contractPath",
    "schemaPath",
    "workloadPath",
    "gatePath",
    "artifactPath",
}


def _load_map() -> dict:
    return json.loads(MAP_PATH.read_text(encoding="utf-8"))


def _entries_by_id(payload: dict) -> dict[str, dict]:
    return {entry["entryId"]: entry for entry in payload["entries"]}


def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_json(path: Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def test_browser_responsibility_map_passes_check() -> None:
    assert map_check.check_responsibility_map(_load_map(), REPO_ROOT) == []


def test_claim_candidate_entries_and_boundaries_have_bindings() -> None:
    payload = _load_map()
    claim_candidates = [
        item
        for item in [*payload["entries"], *payload["boundaries"]]
        if item["scopeStatus"] == "doe_claim_candidate"
    ]

    assert claim_candidates

    for item in claim_candidates:
        assert CLAIM_BINDING_FIELDS <= set(item["claimBinding"])
        for field in CLAIM_BINDING_FIELDS:
            assert item["claimBinding"][field]


def test_boundaries_reference_existing_entries() -> None:
    payload = _load_map()
    entry_ids = set(_entries_by_id(payload))

    for boundary in payload["boundaries"]:
        assert boundary["fromEntryId"] in entry_ids
        assert boundary["toEntryId"] in entry_ids


def test_responsibility_map_checker_rejects_stale_claim_binding() -> None:
    payload = _load_map()
    payload["entries"][0]["scopeStatus"] = "doe_claim_candidate"
    payload["entries"][0]["claimBinding"] = {
        "contractPath": "missing.contract.md",
        "schemaPath": "config/browser-responsibility-map.schema.json",
        "workloadPath": "config/browser-responsibility-map.json",
        "gatePath": "bench/tests/test_browser_responsibility_map.py",
        "artifactPath": "bench/out/**/{*.json,*.claim.json}",
    }

    assert {
        "code": "stale_reference",
        "path": "entries[0].claimBinding.contractPath",
        "message": "claim binding target does not exist: missing.contract.md",
    } in map_check.check_responsibility_map(payload, REPO_ROOT)


def test_responsibility_map_checker_rejects_claim_binding_path_escape() -> None:
    payload = _load_map()
    payload["entries"][0]["scopeStatus"] = "doe_claim_candidate"
    payload["entries"][0]["claimBinding"] = {
        "contractPath": "../AGENTS.md",
        "schemaPath": "config/browser-responsibility-map.schema.json",
        "workloadPath": "config/browser-responsibility-map.json",
        "gatePath": "bench/tests/test_browser_responsibility_map.py",
        "artifactPath": "examples/browser-claim-report.sample.json",
    }

    assert {
        "code": "unsafe_claim_binding_path",
        "path": "entries[0].claimBinding.contractPath",
        "message": "claim binding path must be repo-relative: ../AGENTS.md",
    } in map_check.check_responsibility_map(payload, REPO_ROOT)


def test_responsibility_map_checker_rejects_stale_boundary_endpoint() -> None:
    payload = _load_map()
    payload["boundaries"][0]["toEntryId"] = "missing_entry"

    assert {
        "code": "stale_reference",
        "path": "boundaries[0].toEntryId",
        "message": "boundary references missing entry 'missing_entry'",
    } in map_check.check_responsibility_map(payload, REPO_ROOT)


def test_browser_gate_formats_responsibility_map_failures(tmp_path: Path) -> None:
    browser_gate = _load_module(BROWSER_GATE_PATH, "browser_gate_for_responsibility_map_test")
    payload = _load_map()
    payload["boundaries"][0]["toEntryId"] = "missing_entry"
    map_path = tmp_path / "browser-responsibility-map.json"
    _write_json(map_path, payload)

    assert browser_gate.validate_responsibility_map(map_path, REPO_ROOT) == [
        "responsibility-map:stale_reference: boundaries[0].toEntryId: "
        "boundary references missing entry 'missing_entry'"
    ]


def test_browser_claim_gate_formats_responsibility_map_failures(tmp_path: Path) -> None:
    browser_claim_gate = _load_module(
        BROWSER_CLAIM_GATE_PATH,
        "browser_claim_gate_for_responsibility_map_test",
    )
    payload = _load_map()
    payload["boundaries"][0]["toEntryId"] = "missing_entry"
    map_path = tmp_path / "browser-responsibility-map.json"
    _write_json(map_path, payload)

    assert browser_claim_gate.responsibility_map_failures(map_path, REPO_ROOT) == [
        "responsibility-map:stale_reference: boundaries[0].toEntryId: "
        "boundary references missing entry 'missing_entry'"
    ]
