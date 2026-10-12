"""Compile a frozen projection and exercise direct native SPIR-V interchange."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

USAGES = {"MAP_READ": 1, "COPY_SRC": 4, "COPY_DST": 8, "UNIFORM": 64, "STORAGE": 128}
BINDINGS = {"uniform": 0, "read-only-storage": 1, "storage": 2}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str], log: Path, env: dict[str, str] | None = None) -> dict:
    begin = time.monotonic_ns()
    completed = subprocess.run(command, capture_output=True, env=env, timeout=120)
    elapsed = (time.monotonic_ns() - begin) / 1_000_000
    log.write_bytes(completed.stdout + b"\nSTDERR\n" + completed.stderr)
    return {"command": command, "returnCode": completed.returncode,
            "processWallMs": elapsed, "logSha256": digest(log),
            "stdout": completed.stdout.decode(errors="replace")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["fixture", "doe-compiler", "tint", "spirv-val", "doe-library",
                 "dawn-library", "doe-executor", "dawn-executor", "custody-layer", "output"]:
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--iterations", type=int, required=True)
    parser.add_argument("--warmup", type=int, required=True)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10000 or not 0 <= args.warmup <= 10000:
        raise ValueError("Invalid frozen sample policy")
    paths = {name: value.resolve() for name, value in vars(args).items()
             if isinstance(value, Path)}
    output, fixture = paths["output"], paths["fixture"]
    output.mkdir()
    manifest_path = fixture / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["schema"] != "doppler.w8a8-projection-fixture/v1":
        raise ValueError("Unknown projection fixture")
    for name, record in manifest["files"].items():
        if Path(name).name != name or digest(fixture / name) != record["sha256"]:
            raise ValueError("Changed fixture payload: " + name)
    report = {"schemaVersion": 1, "classification": "compiler-runtime-attribution-diagnostic",
              "fixtureManifestSha256": digest(manifest_path),
              "shape": manifest["shape"], "inputs": {}, "compilations": [], "arms": [],
              "sampling": {"iterations": args.iterations, "warmup": args.warmup},
              "performanceClaim": False}
    for name, path in paths.items():
        if name not in {"output", "fixture"}:
            report["inputs"][name] = {"path": str(path), "sha256": digest(path)}
    compiled = {}
    for compiler in ["doe", "tint"]:
        folder = output / compiler
        folder.mkdir()
        rows = []
        for stage in manifest["stages"]:
            target = folder / (stage["id"] + ".spv")
            overrides = ",".join(f"{key}={int(value) if isinstance(value, bool) else value}"
                                 for key, value in stage["constants"].items())
            if compiler == "doe":
                command = [str(paths["doe_compiler"]), "--shader-path",
                           str(fixture / stage["shader"]), "--out", str(target),
                           "--mode", "vulkan-compute-runtime", "--overrides", overrides]
            else:
                command = [str(paths["tint"]), str(fixture / stage["shader"]),
                           "--format", "spirv", "--output-name", str(target),
                           "--entry-point", stage["entryPoint"], "--overrides", overrides]
            row = run(command, folder / (stage["id"] + ".compile.log"))
            row.pop("stdout")
            row |= {"compiler": compiler, "stage": stage["id"],
                    "sourceSha256": digest(fixture / stage["shader"]),
                    "constants": stage["constants"], "spirvSha256": None,
                    "validation": None}
            if row["returnCode"] == 0 and target.exists():
                row["spirvSha256"] = digest(target)
                validation = run([str(paths["spirv_val"]), "--target-env", "vulkan1.1", str(target)],
                                 folder / (stage["id"] + ".validation.log"))
                validation.pop("stdout"); row["validation"] = validation
                if validation["returnCode"] == 0:
                    compiled[compiler, stage["id"]] = target
            rows.append(row)
        report["compilations"].extend(rows)
    ids = list(manifest["buffers"])
    for compiler in ["tint", "doe"]:
        for runtime in ["dawn", "doe"]:
            arm = {"compiler": compiler, "runtime": runtime, "sequences": [],
                   "directCompilerReplacementQualified": False,
                   "qualification": "unestablished"}
            for sequence, stage_ids in manifest["sequences"].items():
                if not all((compiler, stage_id) in compiled for stage_id in stage_ids):
                    arm["sequences"].append({"sequence": sequence, "status": "failed",
                                             "reason": "Compilation or SPIR-V validation failed"})
                    continue
                lines = ["1 " + str(len(ids))]
                for name in ids:
                    record = manifest["buffers"][name]
                    initial = "zero" if record["initial"] == "zero" else str(fixture / record["initial"])
                    expected = "none"
                    if "expected" in record:
                        expected = str(fixture / record["expected"])
                    elif "expectedU32" in record:
                        target = output / (name + ".expected.bin")
                        target.write_bytes(int(record["expectedU32"]).to_bytes(4, "little"))
                        expected = str(target)
                    usage = sum(USAGES[item] for item in record["usage"])
                    lines.append(f"{record['size']} {usage} {initial} {expected}")
                lines.append(str(len(stage_ids)))
                for stage_id in stage_ids:
                    stage = next(row for row in manifest["stages"] if row["id"] == stage_id)
                    if stage["group"] != 0 or any(binding["offset"] for binding in stage["bindings"]):
                        raise ValueError("Executor admits group zero and zero offsets only")
                    x, y, z = stage["dispatch"]
                    lines.append(f"{compiled[compiler, stage_id]} {stage['entryPoint']} {x} {y} {z} {len(stage['bindings'])}")
                    for binding in stage["bindings"]:
                        lines.append(f"{binding['binding']} {ids.index(binding['buffer'])} {BINDINGS[binding['type']]}")
                stem = f"{compiler}-{runtime}-{sequence}"
                plan = output / (stem + ".plan")
                plan.write_text("\n".join(lines) + "\n", encoding="utf-8")
                command = [str(paths[runtime + "_executor"]), str(paths[runtime + "_library"]),
                           str(plan), str(args.iterations), str(args.warmup)]
                custody = output / (stem + ".custody")
                custody.mkdir()
                env = dict(os.environ)
                env.update(VK_LAYER_PATH=str(paths["custody_layer"].parent),
                           VK_INSTANCE_LAYERS="VK_LAYER_DOE_projection_custody",
                           DOE_VULKAN_CUSTODY_DIR=str(custody))
                execution_log = output / (stem + ".execution.log")
                row = run(command, execution_log, env)
                observed = []
                for line in row.pop("stdout").splitlines():
                    if line.startswith("{"):
                        observed.append(json.loads(line))
                captured = []
                journal = custody / "custody.jsonl"
                if journal.exists():
                    for line in journal.read_text(encoding="utf-8").splitlines():
                        record = json.loads(line)
                        artifact = custody / record["artifact"]
                        record["sha256"] = digest(artifact) if artifact.exists() else None
                        captured.append(record)
                expected = Counter(digest(compiled[compiler, stage_id]) for stage_id in stage_ids)
                capabilities_path = custody / "capabilities.jsonl"
                capabilities = [json.loads(line) for line in capabilities_path.read_text().splitlines()] if capabilities_path.exists() else []
                physical = bool(capabilities) and all(record["result"] == 0 and record["vendorId"] == 0x8086 for record in capabilities)
                samples = [record for record in observed if record.get("kind") == "sample"]
                # A successful process alone cannot establish execution or physical custody.
                verified = physical and len(samples) == args.iterations + args.warmup and all(record["exact"] for record in samples)
                direct = bool(captured) and all(record["retained"] and record["result"] == 0 for record in captured)
                direct &= Counter(record["sha256"] for record in captured) == expected
                status = "passed" if row["returnCode"] == 0 and verified else "failed"
                reason = None
                if "SPIR-V extension 'SPV_KHR_integer_dot_product' is not supported" in execution_log.read_text(encoding="utf-8"):
                    status, reason = "unsupported", "Dawn's Tint SPIR-V reader rejects the integer-dot extension"
                row |= {"sequence": sequence, "planSha256": digest(plan), "observations": observed,
                        "status": status, "unsupportedReason": reason,
                        "vulkanCustody": captured, "compilerProductPreserved": direct}
                row["deviceCapabilities"] = capabilities
                arm["sequences"].append(row)
            if all(row["status"] == "passed" for row in arm["sequences"]):
                arm["qualification"] = "exact-projection-output-passed"
                arm["directCompilerReplacementQualified"] = all(row["compilerProductPreserved"] for row in arm["sequences"])
            report["arms"].append(arm)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for arm in report["arms"]:
        print(arm["compiler"], arm["runtime"], arm["qualification"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
