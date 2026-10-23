#!/usr/bin/env python3
"""Freeze native integration selection without inspecting candidate source."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

CASE_SEAL = "98a6656e0b3bf2bb2a6e74bb5977afabdab2ac2ac07c664468a8026dea43b281"
CONTRACT_SEAL = "f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4"
M11_INPUTS = "c5810c4b1fd2883469f749f9fc4a1077f8be1e2570f3d40bb99404dfdb496e17"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    assert sha(args.cases / "CASE_SEAL.sha256") == CASE_SEAL
    assert sha(args.repo / "runtime/c/compiler/CONTRACT_12.sha256") == CONTRACT_SEAL
    assert sha(args.native / "PORTABLE_NATIVE_INPUTS.json") == M11_INPUTS
    rows = json.loads((args.cases / "cases.json").read_text())["cases"]
    cases = {row["id"]: row for row in rows}
    plan = json.loads((args.cases / "GATE_PLAN.json").read_text())
    names = []
    operations = ("min", "max", "clamp", "exp", "tanh", "sqrt", "dot",
                  "length", "distance", "normalize", "cross")
    for operation in operations:
        width = 3 if operation == "cross" else 2
        for stage in ("module", "local", "runtime"):
            names.append(f"{stage}_{operation}_f32_{width}")
    for operation in ("min", "max", "clamp", "dot"):
        for leaf in ("i32", "u32"):
            for stage in ("module", "runtime"):
                names.append(f"{stage}_{operation}_{leaf}_4")
    names += [
        "runtime_clamp_f32_4", "runtime_reversed_clamp_u32_4",
        "ai_products_cancel_0", "ai_positive_partial_cancel_0",
        "ai_negative_partial_cancel_0", "ai_four_large_cancel_0",
        "ai_negative_large_cancel_0", "ai_min_exact_0",
        "ai_explicit_f32_conversion", "const_width_graph_AF",
        "const_width_graph_f32", "ai_final_high_0", "ai_final_low_0",
        "ai_magnitude_2pow128_0", "ai_explicit_argument_product",
        "ai_explicit_argument_addition", "ai_default_i32_range",
        "bad_domain_sqrt_negative_lane", "bad_domain_normalize_zero",
        "bad_domain_exp_overflow_lane", "bad_domain_dot_f32_intermediate_overflow",
        "bad_domain_length_f32_intermediate_overflow",
        "bad_domain_distance_f32_intermediate_overflow", "bad_scalar_dot",
        "bad_scalar_normalize", "bad_cross_width_4", "bad_width_clamp",
        "bad_generic_cross", "bad_mixed_dot_i32_u32", "bad_bool_min_live",
        "bad_arity_cross_1", "bad_statement_dot", "shadow_value_dot_local",
        "bad_clamp_bounds_f32_4_3_skip", "bad_clamp_runtime_value_f32_plain",
        "bad_clamp_runtime_value_f32_module_bounds",
        "bad_clamp_runtime_value_f32_local_bounds", "uniformity_varying_dot_1",
        "uniformity_skipped_clamp_2"
    ]
    names = list(dict.fromkeys(names))
    selected = []
    for name in names:
        row = cases[name]
        assert row["expected_status"] in (0, 4)
        assert sha(args.cases / row["source"]) == row["source_sha256"]
        native_row = {key: row[key] for key in
                      ("id", "source", "source_sha256", "expected_status")}
        source = (args.cases / row["source"]).read_text()
        # Frozen public ABI1/v2/job scalar resource tags encode only u32/f32.
        # This narrow mapping is derived from exact unchanged source bytes,
        # not from a candidate result or a broad old-negative rewrite.
        unrepresentable = re.findall(
            r"var\s*<\s*storage\s*,\s*(?:read|read_write)\s*>\s*"
            r"(\w+)\s*:\s*array\s*<\s*i32\s*>\s*;", source)
        native_row["legacy_expected_status"] = (
            3 if row["expected_status"] == 0 and unrepresentable
            else row["expected_status"])
        native_row["legacy_projection_name"] = (
            unrepresentable[0] if row["expected_status"] == 0 and unrepresentable
            else "")
        selected.append(native_row)
    pool_rows = {}
    for key, spec in plan["pools"].items():
        row = cases[spec["seed"]]
        assert row["expected_status"] == 0
        pool_rows[key] = {name: row[name] for name in
                          ("id", "source", "source_sha256", "expected_status")}
    data = {
        "case_seal_sha256": CASE_SEAL,
        "contract_seal_sha256": CONTRACT_SEAL,
        "m11_input_metadata_sha256": M11_INPUTS,
        "selected_cases": selected,
        "pools": pool_rows,
        "concurrency_case": "runtime_clamp_f32_4",
        "semantic_consistency": "Raw per-ABI output is independently consumed by the parent sealed model, not compared for binary-ID equality.",
        "legacy_projection": "Exactly four runtime i32 source resources require named legacy projection UNSUPPORTED; typed admission remains success. Others are scalar u32/f32 storage arrays with no structs. No aggregate projection waiver.",
        "host_profiles": 32,
        "modes": [1, 2, 3, 5, 6, 7],
        "preserved_pools": ["T", "S", "R", "X"],
        "preserved_pool_front_door": "Exact unchanged capacity_m10.c in candidate2-consumers/commands.json recipes",
        "candidate_inspected": False,
        "candidate_built": False,
        "historical_expectations_changed": False
    }
    args.out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    print(f"Prepared {len(selected)} native selected cases and {len(pool_rows)} pools")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
