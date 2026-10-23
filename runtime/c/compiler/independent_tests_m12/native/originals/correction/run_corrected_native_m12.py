#!/usr/bin/env python3
"""Additive one-ID native evaluator correction; original runner stays sealed."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
SPEC_HASH = '02c15470677a886e151c2822fe8837570eab46629e1fec59b125cd5060cf5026'

def check(path, expected):
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == expected, str(path)

check(HERE / "CORRECTION.json", SPEC_HASH)
correction = json.loads((HERE / "CORRECTION.json").read_text())
for name in ("authoritative_correction", "scalar_adjudication", "original_runner", "original_native_seal", "original_native_first_failure"):
    check(correction[name], correction[name + "_sha256"])
spec = importlib.util.spec_from_file_location("m12_original_native", correction["original_runner"])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original = module.immutable_inputs

def corrected_inputs(args):
    inputs, pins, identities = original(args)
    assert args.stage in ("numeric", "sanitize"), "Correction scoped to numeric/native sanitizer stage"
    official = json.loads(Path(correction["authoritative_correction"]).read_text())
    approved = [r for r in official["corrections"] if r["id"] == correction["id"]]
    assert len(approved) == 1
    assert approved[0]["source_sha256"] == correction["source_sha256"]
    assert approved[0]["original_expected_status"] == correction["old_status"]
    assert approved[0]["corrected_expected_status"] == correction["new_status"]
    rows = [r for r in inputs["selected_cases"] if r["id"] == correction["id"]]
    assert len(rows) == 1
    row = rows[0]
    assert row["source"] == correction["source"]
    assert row["source_sha256"] == correction["source_sha256"]
    check(args.cases / row["source"], correction["source_sha256"])
    assert row["legacy_projection_name"] == ""
    for field in correction["fields"]:
        assert row[field] == correction["old_status"]
        row[field] = correction["new_status"]
    identities["additive_exact_correction"] = str(HERE / "CORRECTION.json")
    identities["additive_exact_correction_sha256"] = SPEC_HASH
    identities["additive_exact_correction_runner_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    identities["original_native_seal_unchanged"] = True
    return inputs, pins, identities

module.immutable_inputs = corrected_inputs
raise SystemExit(module.main())
