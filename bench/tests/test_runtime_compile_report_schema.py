#!/usr/bin/env python3
"""Tests for the runtime compile report schema contract."""

from __future__ import annotations

import json
from pathlib import Path

import jsonschema


REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "config" / "runtime-compile-report.schema.json"
SAMPLE_PATH = REPO_ROOT / "examples" / "runtime-compile-report.sample.json"


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_runtime_compile_report_sample_matches_schema() -> None:
    schema = _load_json(SCHEMA_PATH)
    sample = _load_json(SAMPLE_PATH)

    jsonschema.Draft202012Validator(schema).validate(sample)

    assert sample["outputBytes"] == sample["mslBytes"]
