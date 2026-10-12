"""Attribute a frozen projection through Doe; never promote application claims."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess

import jsonschema

USAGES = {"MAP_READ": 1, "COPY_SRC": 4, "COPY_DST": 8, "UNIFORM": 64, "STORAGE": 128}
BINDINGS = {"uniform": 0, "read-only-storage": 1, "storage": 2}
ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate(value: dict) -> None:
    jsonschema.Draft202012Validator(load(
        ROOT / "config/projection-cost.schema.json")).validate(value)


def drm_clients() -> dict:
    """Retain observable clients without claiming unreadable processes are idle."""
    clients, unreadable = [], 0
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal():
            continue
        try:
            observations = []
            for entry in (process / "fdinfo").iterdir():
                fields = [line for line in entry.read_text().splitlines()
                          if line.startswith("drm-")]
                if fields:
                    observations.append(fields)
            if observations:
                clients.append({"pid": int(process.name),
                                "name": (process / "comm").read_text().strip(),
                                "fdinfo": observations})
        except (PermissionError, FileNotFoundError, ProcessLookupError):
            unreadable += 1
    return {"clients": clients, "unreadableProcesses": unreadable,
            "exclusiveGpuAccessEstablished": False}


def make_plan(manifest: dict, fixture: Path, programs: Path,
              sequence: str, output: Path) -> Path:
    """Keep shaders, bindings, operands and independent oracles frozen."""
    ids = list(manifest["buffers"])
    status = manifest["buffers"]["status"]
    if status["size"] != 4 or status.get("expectedU32") != 0:
        raise ValueError("Resident completion requires a four-byte zero status")
    lines = [f"2 {len(ids)} {ids.index('status')}"]
    for name, record in manifest["buffers"].items():
        initial = "zero" if record["initial"] == "zero" else str(fixture / record["initial"])
        expected = "none"
        if "expected" in record:
            expected = str(fixture / record["expected"])
        elif "expectedU32" in record:
            oracle = output / (name + ".expected.bin")
            oracle.write_bytes(int(record["expectedU32"]).to_bytes(4, "little"))
            expected = str(oracle)
        usage = sum(USAGES[item] for item in record["usage"])
        lines.append(f"{record['size']} {usage} {initial} {expected}")
    stage_ids = manifest["sequences"][sequence]
    lines.append(str(len(stage_ids)))
    for stage_id in stage_ids:
        stage = next(s for s in manifest["stages"] if s["id"] == stage_id)
        if stage["group"] or any(b["offset"] for b in stage["bindings"]):
            raise ValueError("Only frozen group-zero, zero-offset bindings supported")
        x, y, z = stage["dispatch"]
        lines.append(f"{programs / (stage_id + '.spv')} {stage['entryPoint']} "
                     f"{x} {y} {z} {len(stage['bindings'])}")
        for binding in stage["bindings"]:
            lines.append(f"{binding['binding']} {ids.index(binding['buffer'])} "
                         f"{BINDINGS[binding['type']]}")
    if any(any(c.isspace() for c in str(p)) for p in [fixture, programs, output]):
        raise ValueError("Native plan paths cannot contain whitespace")
    plan = output / f"{programs.name}-{sequence}.plan"
    plan.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return plan


def summarize(rows: list[dict], iterations: int, warmup: int,
              mode: str, transfer: str, full_bytes: int) -> dict:
    """Reject absent completion, transfers, verification or phase observations."""
    if not any(r.get("kind") == "loadedLibrary" and r.get("observed") for r in rows):
        raise ValueError("Library ownership was not observed")
    if not any(r.get("kind") == "adapter" and r.get("physicalSelectedVulkan") for r in rows):
        raise ValueError("Wrong physical adapter")
    if rows[-1] != {"kind": "cleanup", "passed": True}:
        raise ValueError("Missing successful settlement")
    markers = [r for r in rows if r.get("kind") == "instrumentation"]
    if len(markers) != 1 or markers[0]["mode"] != mode or markers[0]["transfer"] != transfer:
        raise ValueError("Instrumentation/transfer mode changed")
    samples = [r for r in rows if r["kind"] == "sample"]
    checks = [r for r in rows if r["kind"] == "verification"]
    if [s["iteration"] for s in samples] != list(range(-warmup, iterations)):
        raise ValueError("Missing, reordered or duplicated samples")
    if [r["iteration"] for r in checks] != [-warmup - 1, iterations]:
        raise ValueError("Both independent full-output checks are required")
    for row in samples + checks:
        full = row["kind"] == "verification" or transfer == "verification"
        if (row["exact"] is not True or row["dispatches"] != 4
                or row["readbackBytes"] != (full_bytes if full else 4)
                or row["readbackCount"] != (5 if full else 1)):
            raise ValueError("Missing execution, output, status or oracle work")
        for name in ["ms", "cpuMs"]:
            if not math.isfinite(row[name]) or row[name] < 0 or (name == "ms" and not row[name]):
                raise ValueError("Invalid timing")
        if (mode == "none") != (row["phases"] is None):
            raise ValueError("Host instrumentation boundary changed")
        if mode != "none":
            phases = row["phases"]
            for name in ["recordMs", "submitMs", "waitMs", "mapMs", "compareMs", "timestampReadMs"]:
                if not math.isfinite(phases[name]) or phases[name] < 0:
                    raise ValueError("Invalid host phase")
            if sum(v for k, v in phases.items() if k.endswith("Ms")) > row["ms"] + 0.001:
                raise ValueError("Host phases exceed measured wall boundary")
        gpu = row["gpuStageNs"]
        if mode == "gpu":
            if len(gpu) != 4 or any(not isinstance(n, int) or n <= 0 for n in gpu):
                raise ValueError("Missing GPU stage observation")
        elif gpu:
            raise ValueError("Undeclared GPU instrumentation")
    timed = [s for s in samples if s["iteration"] >= 0]
    summary = {"medianMs": statistics.median(s["ms"] for s in timed),
               "medianCpuMs": statistics.median(s["cpuMs"] for s in timed),
               "phases": None, "gpuStageNs": []}
    if mode != "none":
        summary["phases"] = {k: statistics.median(s["phases"][k] for s in timed)
                             for k in timed[0]["phases"]}
    if mode == "gpu":
        summary["gpuStageNs"] = [statistics.median(s["gpuStageNs"][i] for s in timed)
                                 for i in range(4)]
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["fixture", "programs", "program-manifest", "library", "executor",
                 "custody-layer", "policy", "output", "icd", "spirv-val"]:
        parser.add_argument("--" + name, type=Path, required=True, help=f"Explicit {name} path")
    args = parser.parse_args()
    paths = {k: v.resolve() for k, v in vars(args).items()}
    policy = load(paths["policy"]); validate(policy)
    manifest = load(paths["fixture"] / "manifest.json")
    origin = load(paths["program_manifest"])
    if manifest["shape"] != {"M": 1, "N": 1024, "K": 640}:
        raise ValueError("This frozen diagnostic admits only the retained projection shape")
    if digest(paths["fixture"] / "manifest.json") != origin["fixtureManifestSha256"]:
        raise ValueError("Compiled programs belong to another fixture")
    for name, record in manifest["files"].items():
        if Path(name).name != name or digest(paths["fixture"] / name) != record["sha256"]:
            raise ValueError("Changed frozen payload: " + name)
    paths["output"].mkdir()
    output = paths["output"]
    report = {"schemaVersion": 1, "kind": "projection-cost-report",
              "performanceClaim": False, "inputs": {}, "runs": [],
              "passed": False, "failure": None}
    for key, path in paths.items():
        if path.is_file():
            report["inputs"][key] = {"path": str(path), "sha256": digest(path)}
    report["inputs"]["fixtureManifest"] = {"path": str(paths["fixture"] / "manifest.json"),
        "sha256": digest(paths["fixture"] / "manifest.json")}
    for source in [Path(__file__), ROOT / "bench/executors/projection-cost-native.c"]:
        report["inputs"][source.name] = {"path": str(source), "sha256": digest(source)}
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("VK_", "DOE_", "MESA_", "RADV_"))}
    env.update(VK_DRIVER_FILES=str(paths["icd"]), MESA_SHADER_CACHE_DISABLE="true")
    full_bytes = sum(b["size"] for b in manifest["buffers"].values()
                     if "expected" in b or "expectedU32" in b)
    try:
        plans = {}
        for compiler in ["tint", "doe"]:
            for stage in manifest["stages"]:
                shader = paths["programs"] / compiler / (stage["id"] + ".spv")
                authored = next(r for r in origin["compilations"]
                                if r["compiler"] == compiler and r["stage"] == stage["id"])
                if digest(shader) != authored["spirvSha256"]:
                    raise ValueError("Changed retained compiler output")
                if (authored["sourceSha256"] != digest(paths["fixture"] / stage["shader"])
                        or authored["constants"] != stage["constants"]):
                    raise ValueError("Compiler source or specialization mismatch")
                checked = subprocess.run([str(paths["spirv_val"]), "--target-env", "vulkan1.1", str(shader)],
                                         capture_output=True, timeout=30)
                (output / f"{compiler}-{stage['id']}.validation.log").write_bytes(checked.stdout + checked.stderr)
                if checked.returncode:
                    raise ValueError("Retained SPIR-V validation failed")
                report["inputs"][f"{compiler}/{stage['id']}"] = {"path": str(shader), "sha256": digest(shader)}
            for sequence in ["portable", "packed"]:
                plans[compiler, sequence] = make_plan(manifest, paths["fixture"], paths["programs"] / compiler, sequence, output)

        def execute(compiler: str, sequence: str, mode: str, transfer: str,
                    label: str, custody: bool = False) -> None:
            folder = output / label; folder.mkdir()
            local_env = env | {"XDG_CACHE_HOME": str(folder / "cache")}
            if custody:
                local_env.update(VK_LAYER_PATH=str(paths["custody_layer"].parent),
                                 VK_INSTANCE_LAYERS="VK_LAYER_DOE_projection_custody",
                                 DOE_VULKAN_CUSTODY_DIR=str(folder))
            command = [str(paths["executor"]), str(paths["library"]),
                       str(plans[compiler, sequence]), str(policy["iterations"]),
                       str(policy["warmup"]), str(policy["vendorId"]),
                       str(policy["deviceId"]), mode, transfer]
            save(folder / "before.json", drm_clients())
            save(folder / "command.json", {"command": command,
                 "environment": {k: local_env[k] for k in local_env if k.startswith(("VK_", "MESA_", "DOE_", "XDG_CACHE"))}})
            try:
                run = subprocess.run(command, capture_output=True, env=local_env,
                                     timeout=policy["processTimeoutSeconds"])
            except subprocess.TimeoutExpired as error:
                (folder / "stdout.jsonl").write_bytes(error.stdout or b"")
                (folder / "stderr.log").write_bytes(error.stderr or b"")
                save(folder / "after.json", drm_clients())
                raise
            (folder / "stdout.jsonl").write_bytes(run.stdout)
            (folder / "stderr.log").write_bytes(run.stderr)
            save(folder / "after.json", drm_clients())
            row = {"label": label, "compiler": compiler, "sequence": sequence,
                   "mode": mode, "transfer": transfer, "custody": custody,
                   "returnCode": run.returncode, "logSha256": digest(folder / "stdout.jsonl"),
                   "summary": None}
            report["runs"].append(row)
            if run.returncode:
                raise ValueError("Native execution failed: " + label)
            rows = [json.loads(line) for line in run.stdout.decode().splitlines() if line.startswith("{")]
            row["summary"] = summarize(rows, policy["iterations"], policy["warmup"], mode, transfer, full_bytes)
            if custody:
                journal = [json.loads(line) for line in (folder / "custody.jsonl").read_text().splitlines()]
                observed = Counter(digest(folder / entry["artifact"]) for entry in journal
                                   if entry["result"] == 0 and entry["retained"])
                expected = Counter(digest(paths["programs"] / compiler / (stage + ".spv"))
                                   for stage in manifest["sequences"][sequence])
                if observed != expected:
                    raise ValueError("Observed Vulkan shaders differ from selected programs")
            save(output / "report.json", report)

        arms = [(c, s) for c in ["tint", "doe"] for s in ["portable", "packed"]]
        for compiler, sequence in arms:
            execute(compiler, sequence, "none", "resident", f"custody-{compiler}-{sequence}", True)
        # Balanced repetitions include distinct, byte-identical A/A processes.
        treatments = [("none-a", "none"), ("none-b", "none"), ("host", "host"), ("gpu", "gpu")]
        combinations = [(c, s, t, label, mode) for c, s in arms
                        for t in ["resident", "verification"] for label, mode in treatments]
        for repeat in range(policy["repeats"]):
            ordered = combinations if repeat % 2 == 0 else list(reversed(combinations))
            for compiler, sequence, transfer, label, mode in ordered:
                execute(compiler, sequence, mode, transfer,
                        f"r{repeat}-{compiler}-{sequence}-{transfer}-{label}")
        report["passed"] = True
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        report["failure"] = str(error)
    validate(report)
    save(output / "report.json", report)
    print(json.dumps({"passed": report["passed"], "failure": report["failure"],
                      "report": str(output / "report.json")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
