"""Run the unchanged real emitter once per frozen M4 corpus path for M5.

Emission and Vulkan validation are separate gates. This stage neither executes
SPIR-V nor qualifies a physical backend or actual runtime adoption.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

CONSUMER_SHA256 = "f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b"
LEDGER_SHA256 = "da60e89733eb6d12c7a128f0d7ac2cfedf278684a6b6a4dd8659a4e81fd0de0c"
WITNESS = "bench/kernels/package_vector_scale_add.wgsl"
WITNESS_SHA256 = "8f2b3e2bb6955c1f7507936773589195c80e6a119cbd50d975f652d4f834e8c1"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_blocker(source: str) -> dict[str, object]:
    """Identify one concrete source feature still excluded by frozen M5.

    This is not a parser or a prediction of the candidate's first diagnostic.
    It records a source-bound reason why a previously rejected source remains
    outside the contract, independently of what the candidate subsequently does.
    """
    patterns = [
        ("extension directive", r"\benable\s+\w+"),
        ("workgroup variables", r"var\s*<\s*workgroup\s*>"),
        ("loops", r"\b(?:loop|for|while)\s*[({]"),
        ("vector storage elements", r"array\s*<\s*vec\w*"),
        ("mutable local variable", r"\bvar\s+\w+"),
        ("bitwise xor", r"\^"),
        ("invocation y/z expression", r"\bgid\.[yz]"),
        ("division", r"/(?!=|[/*])"),
        ("non-global invocation builtin", r"@builtin\((?:workgroup_id|local_invocation_id)\)"),
        ("non-compute stage", r"@(?:vertex|fragment)\b"),
    ]
    # Preserve byte positions while hiding comment contents from the witness scan.
    clean = re.sub(r"//[^\n]*|/\*.*?\*/", lambda m: " " * len(m[0]),
                   source, flags=re.S)
    for label, pattern in patterns:
        match = re.search(pattern, clean)
        if match:
            offset = len(source[:match.start()].encode("utf-8"))
            return {"reason": label, "byte_offset": offset,
                    "source_fragment": match[0]}
    functions = list(re.finditer(r"\bfn\s+\w+", clean))
    if len(functions) > 1:
        return {"reason": "multiple functions/helpers", "count": len(functions),
                "source_fragment": functions[1][0]}
    resources = list(re.finditer(r"var\s*<\s*(?:storage|uniform)\b", clean))
    if len(resources) > 8:
        return {"reason": "more than eight resources", "count": len(resources)}
    if not resources:
        return {"reason": "zero resources excluded", "count": 0}
    raise ValueError("Frozen rejected source needs manual admission classification")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "cli", "validator", "evidence"):
        parser.add_argument("--" + name, type=Path, required=True,
                            help="Explicit input or fresh evidence directory.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.repo = args.repo.resolve()
    args.cli = args.cli.resolve()
    args.validator = args.validator.resolve()
    args.evidence = args.evidence.resolve()
    args.evidence.mkdir(parents=True, exist_ok=False)
    consumer = args.repo / "bench/gates/spirv_val_gate.py"
    ledger_path = args.repo / "docs/c-migration/corpus-coverage-m4.json"
    if sha(consumer.read_bytes()) != CONSUMER_SHA256:
        raise ValueError("Unchanged real consumer hash differs from frozen input")
    if sha(ledger_path.read_bytes()) != LEDGER_SHA256:
        raise ValueError("Frozen M4 corpus ledger hash differs")
    ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
    rows = ledger["records"]
    if len(rows) != 98 or len({x["source_sha256"] for x in rows}) != 91:
        raise ValueError("Frozen corpus is not 98 paths / 91 unique sources")
    spec = importlib.util.spec_from_file_location("real_spirv_gate", consumer)
    if spec is None or spec.loader is None:
        raise ImportError(str(consumer))
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    records = []
    for index, old in enumerate(rows):
        source_path = args.repo / old["source"]
        source = source_path.read_bytes()
        if sha(source) != old["source_sha256"]:
            raise ValueError("Source hash changed: " + old["source"])
        expected = bool(old["admitted"] or old["source"] == WITNESS)
        if old["source"] == WITNESS and sha(source) != WITNESS_SHA256:
            raise ValueError("Unchanged witness hash differs")
        classification = ({"reason": "retained M4 admission"} if old["admitted"]
                          else {"reason": "new flat uniform/member-load witness"}
                          if expected else source_blocker(source.decode("utf-8")))
        output_dir = args.evidence / f"source_{index:03d}"
        output_dir.mkdir()
        output = output_dir / "module.spv"
        ok, message = gate.emit_spirv_for_wgsl(str(args.cli), source_path, output)
        errors = []
        if bool(ok) != expected:
            errors.append(f"Expected admission={expected}; observed={ok}")
        if source_path.read_bytes() != source:
            errors.append("Consumer modified original source")
        wanted = {"module.spv"} if ok else set()
        if {p.name for p in output_dir.iterdir()} != wanted:
            errors.append("Missing module, failure residue or unexpected sidecar")
        validation = None
        if ok:
            command = [str(args.validator), "--target-env", "vulkan1.1", str(output)]
            run = subprocess.run(command, capture_output=True, text=True,
                                 timeout=30, check=False)
            validation = {"command": command, "returncode": run.returncode,
                          "stdout": run.stdout, "stderr": run.stderr}
            if run.returncode:
                errors.append("Explicit Vulkan1.1 validation failed")
        diagnostic = re.search(r"category=(\d+) offset=(\d+): (.*)", message)
        observed = None
        if diagnostic:
            offset = int(diagnostic[2])
            observed = {"category": int(diagnostic[1]), "byte_offset": offset,
                        "message": diagnostic[3],
                        "original_source_at_diagnostic":
                            source[offset:offset + 100].decode("utf-8", errors="replace")}
        record = {"source": old["source"], "source_sha256": sha(source),
                  "source_bytes": len(source), "expected_admitted": expected,
                  "admitted": ok, "classification": classification,
                  "consumer_command": [str(args.cli), "--shader-path",
                                       str(source_path), "--out", str(output)],
                  "real_consumer_message": message, "first_diagnostic": observed,
                  "output": str(output) if ok else None,
                  "output_sha256": sha(output.read_bytes()) if ok else None,
                  "validation": validation, "errors": errors, "passed": not errors,
                  "stages": {"emission": "passed" if ok else "rejected",
                             "external_vulkan11_validation":
                                 "passed" if validation and not validation["returncode"]
                                 else "failed" if validation else "not_run_no_module",
                             "independent_instruction_execution": "not_run_this_stage",
                             "physical_backend": "not_run",
                             "runtime_adoption": "not_run"}}
        records.append(record)
        (args.evidence / "records.json").write_text(
            json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if errors:
            print(json.dumps(record, sort_keys=True), flush=True)
    admitted = [r for r in records if r["admitted"]]
    result = {"consumer_sha256": CONSUMER_SHA256,
              "frozen_ledger_sha256": LEDGER_SHA256,
              "cli_sha256": sha(args.cli.read_bytes()),
              "validator_sha256": sha(args.validator.read_bytes()),
              "counts": {"paths": len(records), "unique_sources": 91,
                         "admitted": len(admitted),
                         "unique_admitted": len({r["source_sha256"] for r in admitted}),
                         "validated": sum(bool(r["validation"] and not r["validation"]["returncode"])
                                          for r in records)},
              "failed": [r for r in records if not r["passed"]], "records": records}
    (args.evidence / "results.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "records"},
                     indent=2, sort_keys=True), flush=True)
    return int(bool(result["failed"]))


if __name__ == "__main__":
    raise SystemExit(main())
