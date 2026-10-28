"""Measure fresh C/Zig compiler processes; validate their artifacts separately.

These are translation-process diagnostics, not generated-program GPU timings or
semantic equivalence proofs. SDK acceptance alone never enables a public claim.
The validation mode is standalone so copied, hash-bound outputs can be checked
on another host without installing this module or changing its checkout.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[2]
TARGETS = ("msl", "spirv", "hlsl")
EXTENSIONS = {"msl": "metal", "spirv": "spv", "hlsl": "hlsl"}
CORPUS = {
    "trivial_noop_compute", "trivial_passthrough_vertex", "simple_relu",
    "simple_silu", "simple_scale_f32", "simple_residual_add", "moderate_rmsnorm",
    "moderate_gelu", "moderate_rope", "moderate_softmax", "moderate_layernorm",
    "moderate_matmul_naive", "complex_attention_decode", "complex_matmul_tiled",
    "complex_fused_gemv_dequant", "complex_multi_entry", "stress_many_bindings",
}
INFERENCE = {"attention-decode", "attention-prefill", "gather", "gelu",
             "matmul-gemv", "matmul-tiled", "residual", "rmsnorm", "rope", "sample"}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def changed(path: Path, expected: str) -> bool:
    try:
        return digest(path) != expected
    except OSError:
        return True


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                         encoding="utf-8")
    temporary.replace(path)


def inside(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"artifact escapes its bundle: {relative}")
    return path


def declared_entries(source: str) -> list[dict[str, Any]]:
    """Declare the existing frozen fixtures; do not guess new WGSL semantics."""
    path = Path(source)
    corpus = path.parent.as_posix() == "bench/kernels/compilation-corpus"
    inference = path.parent.as_posix() == "bench/inference-pipeline/kernels"
    if not ((corpus and path.stem in CORPUS) or
            (inference and path.stem in INFERENCE)):
        raise ValueError(f"fixture has no declared entry contract: {source}")
    names = ["main"]
    if corpus and path.stem == "complex_multi_entry":
        names = ["fused_gate_up", "elementwise_add", "scale_bias"]
    elif inference and path.stem == "rmsnorm":
        names += ["main_weight_offset"]
    elif inference and path.stem == "sample":
        names += ["sample_single_pass"]
    stage = "vertex" if path.stem == "trivial_passthrough_vertex" else "compute"
    return [{"name": name, "stage": stage, "selector": index}
            for index, name in enumerate(names)]


def spirv_entries(data: bytes) -> set[tuple[str, str]]:
    if len(data) < 20 or len(data) % 4:
        raise ValueError("invalid SPIR-V byte framing")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError("invalid SPIR-V magic")
    entries: set[tuple[str, str]] = set()
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        if not size or size > len(words) - cursor:
            raise ValueError("invalid SPIR-V instruction framing")
        if opcode == 15:
            if size < 4 or words[cursor + 1] not in (0, 4, 5):
                raise ValueError("unsupported SPIR-V entry record")
            raw = struct.pack(f"<{size - 3}I", *words[cursor + 3:cursor + size])
            if b"\0" not in raw:
                raise ValueError("unterminated SPIR-V entry name")
            name = raw.split(b"\0", 1)[0].decode("utf-8")
            stage = {0: "vertex", 4: "fragment", 5: "compute"}[words[cursor + 1]]
            if (name, stage) in entries:
                raise ValueError("duplicate SPIR-V entry")
            entries.add((name, stage))
        cursor += size
    return entries


def check_entries(data: bytes, target: str, entries: list[dict[str, Any]]) -> None:
    expected = {(entry["name"], entry["stage"]) for entry in entries}
    if target == "spirv":
        if spirv_entries(data) != expected:
            raise ValueError("SPIR-V entry set differs from declared source work")
        return
    text = data.decode("utf-8")
    if target == "msl":
        # The C emitter publishes source entry identity via host_name while
        # Zig's MSL maps reserve main and rename it by its actual stage.
        hosts = {(symbol, stage): name for name, stage, symbol in re.findall(
            r'\[\[host_name\("([^"\\]+)"\)\]\]\s+'
            r'(kernel|vertex|fragment)\s+[\w:<>]+\s+(\w+)\s*\(', text)}
        found = set()
        for keyword, attribute, symbol in re.findall(
                r'(?:\b(kernel|vertex|fragment)|\[\[(kernel|vertex|fragment)\]\])'
                r'\s+[\w:<>]+\s+(\w+)\s*\(', text):
            stage = keyword or attribute
            name = hosts.get((symbol, stage), symbol)
            semantic_stage = "compute" if stage == "kernel" else stage
            if ("main", semantic_stage) in expected and name == "main_" + stage:
                name = "main"
            found.add((name, semantic_stage))
        if found != expected:
            raise ValueError("MSL entry set differs from declared source work")
    else:
        for name, _ in expected:
            if not re.search(r"\b" + re.escape(name) + r"\s*\(", text):
                raise ValueError(f"HLSL lacks declared entry {name}")


def process_sample(command: list[str], folder: Path, timeout: float, *,
                   child_started: Callable[[subprocess.Popen], None] | None = None) -> dict[str, Any]:
    from bench.lib.child_usage import (
        child_cpu_snapshot, completed_child_cpu_ms, mac_resident_kib,
        windows_child_usage,
    )

    folder.mkdir(parents=True)
    stdout, stderr = folder / "stdout.bin", folder / "stderr.txt"
    stop = threading.Event()
    timed_out = threading.Event()
    resident: list[int] = []
    sampling_errors: list[str] = []
    before = child_cpu_snapshot()
    with stdout.open("wb") as output, stderr.open("wb") as errors:
        start = time.perf_counter_ns()
        child = subprocess.Popen(command, stdout=output, stderr=errors,
                                 start_new_session=os.name != "nt")

        def terminate() -> None:
            if child.poll() is None:
                timed_out.set()
                if os.name == "nt":
                    child.kill()
                else:
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass

        def sample_memory() -> None:
            while not stop.is_set():
                try:
                    if sys.platform == "darwin":
                        value = mac_resident_kib(child.pid)
                    elif sys.platform.startswith("linux"):
                        text = Path(f"/proc/{child.pid}/status").read_text()
                        match = re.search(r"^VmRSS:\s+(\d+)", text, re.M)
                        value = int(match[1]) if match else 0
                    else:
                        return
                    if value:
                        resident.append(value)
                except FileNotFoundError:
                    return
                except OSError as error:
                    sampling_errors.append(str(error))
                    return
                stop.wait(0.001)

        watchdog = threading.Timer(timeout, terminate)
        watcher = threading.Thread(target=sample_memory, daemon=True)
        try:
            watchdog.start()
            watcher.start()
            if child_started:
                child_started(child)
            code = child.wait()
            end = time.perf_counter_ns()
        finally:
            # A caller exception or cancellation cannot orphan this session.
            if child.poll() is None:
                terminate()
                child.wait()
            stop.set()
            watchdog.cancel()
            if watcher.ident is not None:
                watcher.join()
            if watchdog.ident is not None:
                watchdog.join()
        cpu = completed_child_cpu_ms(child, before)
        peak = windows_child_usage(child)[1] if os.name == "nt" else None
    return {"command": command, "exitCode": code, "timedOut": timed_out.is_set(),
            "wallNs": end - start, "cpuMs": cpu,
            "peakWorkingSetKiB": peak,
            "sampledResidentLowerBoundKiB": max(resident) if resident else None,
            "residentSampleCount": len(resident), "samplingErrors": sampling_errors,
            "stdout": str(stdout), "stderr": str(stderr),
            "outputSha256": digest(stdout), "outputBytes": stdout.stat().st_size}


def stats(samples: list[dict[str, Any]]) -> dict[str, Any]:
    values = [sample["wallNs"] for sample in samples
              if sample["exitCode"] == 0 and not sample["timedOut"]]
    if not values:
        return {}
    ordered = sorted(values)
    return {"p50Ns": statistics.median(values),
            "p95Ns": ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)],
            "minNs": min(values), "maxNs": max(values), "count": len(values),
            "percentileEstimator": "median; nearest-rank p95; no filtering"}


def measure(args: argparse.Namespace) -> int:
    if args.iterations < 1 or args.warmup < 0:
        raise ValueError("iterations must be positive and warmup nonnegative")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    catalog = args.workloads.resolve()
    rows = [row for row in json.loads(catalog.read_text())["workloads"]
            if row.get("runnerType") == "compilation"]
    if not rows:
        raise ValueError("compiler corpus contains no compilation workloads")
    binaries = {target: {"c": args.c_bin_dir.resolve() / f"doe-emit-{target}-c",
                         "zig": args.zig_bin_dir.resolve() / f"doe-emit-{target}"}
                for target in args.targets}
    paths = {catalog, Path(__file__).resolve(), ROOT / "bench/lib/child_usage.py"}
    for row in rows:
        paths.add(inside(ROOT, row["shaderPath"]))
        row["entries"] = declared_entries(row["shaderPath"])
    for tools in binaries.values():
        paths.update(tools.values())
    for directory in (ROOT / "runtime/c/compiler", ROOT / "runtime/zig/src/compiler",
                      ROOT / "runtime/zig/src/cli/entrypoints"):
        paths.update(path for path in directory.rglob("*") if path.is_file()
                     and path.suffix in (".c", ".h", ".zig")
                     and "build" not in path.relative_to(directory).parts)
    hashes = {str(path): digest(path) for path in sorted(paths)}
    report: dict[str, Any] = {
        "schemaVersion": 1, "scope": "fresh-process WGSL translation only",
        "claimEligible": False, "sourceCommit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "sourceStatus": subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True).splitlines(),
        "host": {"platform": platform.platform(), "machine": platform.machine()},
        "startedAt": datetime.now(timezone.utc).isoformat(), "inputs": hashes,
        "sampling": {"iterations": args.iterations, "warmup": args.warmup,
                     "order": "alternate C/Zig for each paired sample and case",
                     "scope": "Popen through child wait; source read, translation, stdout serialization and exit",
                     "cache": "fresh process; warm filesystem; no resident compiler or subtraction",
                     "memory": "1ms RSS samples are lower bounds; missing observations are null, not zero"},
        "rows": [], "status": "running"}
    manifest: dict[str, Any] = {"schemaVersion": 1, "sourceCommit": report["sourceCommit"], "rows": []}
    write_json(output / "report.json", report)
    for index, (row, target) in enumerate((r, t) for r in rows for t in args.targets):
        print(f"[{index + 1}/{len(rows) * len(args.targets)}] {row['id']} {target}", flush=True)
        shader = inside(ROOT, row["shaderPath"])
        result: dict[str, Any] = {"id": row["id"], "target": target,
                                  "sourceSha256": digest(shader),
                                  "entries": row["entries"], "providers": {}}
        for provider in ("c", "zig"):
            result["providers"][provider] = {"samples": [], "warmup": [], "failures": []}
        for sample_index in range(args.warmup + args.iterations):
            order = ("c", "zig") if (sample_index + index) % 2 == 0 else ("zig", "c")
            for provider in order:
                group = result["providers"][provider]
                folder = output / "samples" / f"{index:03d}" / f"{sample_index:03d}-{provider}"
                command = [str(binaries[target][provider]), "--shader-path", str(shader)]
                try:
                    sample = process_sample(command, folder, args.timeout)
                except OSError as error:
                    sample = {"command": command, "exitCode": None, "timedOut": False,
                              "error": str(error), "outputBytes": 0}
                sample["providerOrder"] = list(order)
                sample["sampleIndex"] = sample_index
                (group["warmup"] if sample_index < args.warmup else group["samples"]).append(sample)
        export_row = {key: result[key] for key in ("id", "target", "sourceSha256", "entries")}
        export_row["providers"] = {}
        for provider, group in result["providers"].items():
            samples = group["warmup"] + group["samples"]
            if any(s["exitCode"] != 0 or s["timedOut"] or not s["outputBytes"] for s in samples):
                group["failures"].append("failed, empty or timed-out translation")
            successful = [s for s in samples if s["exitCode"] == 0 and not s["timedOut"] and s["outputBytes"]]
            if len({s["outputSha256"] for s in successful}) > 1:
                group["failures"].append("output changed between identical fresh-process invocations")
            if successful:
                source = Path(successful[0]["stdout"])
                try:
                    check_entries(source.read_bytes(), target, row["entries"])
                except (ValueError, UnicodeError) as error:
                    group["failures"].append(str(error))
                relative = f"validation-artifacts/{index:03d}/{provider}.{EXTENSIONS[target]}"
                destination = output / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                export_row["providers"][provider] = {
                    "artifact": relative, "sha256": digest(destination),
                    "translationAccepted": not group["failures"]}
            group["statistics"] = stats(group["samples"])
            group["status"] = "failed" if group["failures"] else "translated"
        result["status"] = "failed" if any(g["failures"] for g in result["providers"].values()) else "translated"
        report["rows"].append(result)
        manifest["rows"].append(export_row)
        drift = [str(path) for path in (shader, *binaries[target].values())
                 if changed(path, hashes[str(path)])]
        write_json(output / "report.json", report)
        write_json(output / "validation-manifest.json", manifest)
        if drift:
            report["drift"] = drift
            break
    report["drift"] = sorted(set(report.get("drift", [])) | {
        path for path, value in hashes.items() if changed(Path(path), value)})
    report["status"] = "failed" if report["drift"] or any(
        row["status"] == "failed" for row in report["rows"]) else "translated_pending_validation"
    report["endedAt"] = datetime.now(timezone.utc).isoformat()
    write_json(output / "report.json", report)
    return int(report["status"] == "failed")


def validate(args: argparse.Namespace) -> int:
    manifest_path = args.validate_manifest.resolve()
    root = manifest_path.parent
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    report: dict[str, Any] = {"scope": "SDK target acceptance; no GPU or timing",
                              "manifestSha256": digest(manifest_path),
                              "validatorScriptSha256": digest(Path(__file__)),
                              "host": platform.platform(), "target": args.validate_target, "rows": []}
    validator = args.validator
    if not validator:
        validator = shutil.which("xcrun" if args.validate_target == "msl" else
                                 "spirv-val" if args.validate_target == "spirv" else "dxc")
    tool = Path(validator).resolve() if validator else None
    if tool and not tool.is_file():
        tool = None
    if tool and args.validate_target == "msl":
        found = subprocess.run([str(tool), "--sdk", "macosx", "--find", "metal"],
                               capture_output=True, text=True, timeout=20)
        report["metalDiscovery"] = {"exitCode": found.returncode, "stdout": found.stdout, "stderr": found.stderr}
        if found.returncode == 0:
            actual = Path(found.stdout.strip())
            report["sdkCompiler"] = {"path": str(actual), "sha256": digest(actual)}
            version = subprocess.run([str(actual), "--version"],
                                     capture_output=True, text=True, timeout=20)
            sdk = subprocess.run([str(tool), "--sdk", "macosx", "--show-sdk-version"],
                                 capture_output=True, text=True, timeout=20)
            report["sdkCompiler"]["version"] = version.stdout + version.stderr
            report["sdkVersion"] = sdk.stdout.strip()
        else:
            tool = None
    if tool:
        report["validator"] = {"path": str(tool), "sha256": digest(tool)}
        if args.validate_target != "msl":
            version = subprocess.run([str(tool), "--version"],
                                     capture_output=True, text=True, timeout=20)
            report["validator"]["version"] = version.stdout + version.stderr
    logs = args.validation_out.resolve()
    logs.mkdir(parents=True, exist_ok=False)
    for index, row in enumerate(manifest["rows"]):
        if row["target"] != args.validate_target:
            continue
        for provider, artifact in row["providers"].items():
            result: dict[str, Any] = {"id": row["id"], "provider": provider,
                                      "artifactSha256": artifact["sha256"], "runs": []}
            path = inside(root, artifact["artifact"])
            if changed(path, artifact["sha256"]):
                result.update(status="failed", reason="artifact hash mismatch")
            elif not tool:
                result.update(status="not_run", reason="target SDK validator unavailable")
            else:
                try:
                    check_entries(path.read_bytes(), args.validate_target, row["entries"])
                    entries = row["entries"] if args.validate_target == "hlsl" else [None]
                    for entry_index, entry in enumerate(entries):
                        prefix = logs / f"{index:03d}-{provider}-{entry_index}"
                        if args.validate_target == "msl":
                            command = [str(tool), "--sdk", "macosx", "metal", "-std=metal3.0", "-c", str(path), "-o", str(prefix)+".air"]
                        elif args.validate_target == "spirv":
                            command = [str(tool), "--target-env", "vulkan1.1", str(path)]
                        else:
                            native16 = "float16_t" in path.read_text(encoding="utf-8")
                            profile = {"compute": "cs", "vertex": "vs", "fragment": "ps"}[entry["stage"]]+("_6_2" if native16 else "_6_0")
                            command = [str(tool), "-T", profile, "-E", entry["name"], str(path), "-Fo", str(prefix)+".dxil"]
                            if provider == "c":
                                command += ["-D", f"DOE_HLSL_ENTRY={entry['selector']}"]
                            if native16:
                                command += ["-enable-16bit-types"]
                        run = subprocess.run(command, capture_output=True, timeout=20)
                        Path(str(prefix)+".stdout").write_bytes(run.stdout)
                        Path(str(prefix)+".stderr").write_bytes(run.stderr)
                        result["runs"].append({"command": command, "entry": entry, "exitCode": run.returncode,
                                                "stdout": str(prefix)+".stdout", "stderr": str(prefix)+".stderr"})
                    result["status"] = "passed" if all(r["exitCode"] == 0 for r in result["runs"]) else "failed"
                except (ValueError, UnicodeError, OSError, subprocess.TimeoutExpired) as error:
                    result.update(status="failed", reason=str(error))
            if changed(path, artifact["sha256"]):
                result.update(status="failed", reason="artifact changed during validation")
            result["translationAccepted"] = artifact["translationAccepted"]
            report["rows"].append(result)
            write_json(logs / "validation.json", report)
    report["status"] = "passed" if report["rows"] and all(
        r["status"] == "passed" for r in report["rows"]) else "incomplete_or_failed"
    write_json(logs / "validation.json", report)
    return int(report["status"] != "passed")


def finalize(report_path: Path, receipts: list[Path]) -> int:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if not report.get("rows"):
        raise ValueError("no measured compiler rows to compare")
    manifest_hash = digest(report_path.parent / "validation-manifest.json")
    script_hash = report["inputs"][str(Path(__file__).resolve())]
    accepted: dict[tuple[str, str, str], dict[str, Any]] = {}
    for path in receipts:
        receipt = json.loads(path.read_text(encoding="utf-8"))
        if receipt["manifestSha256"] != manifest_hash or receipt["validatorScriptSha256"] != script_hash:
            raise ValueError(f"validator receipt is not bound to this frozen campaign: {path}")
        for row in receipt["rows"]:
            key = (row["id"], receipt["target"], row["provider"])
            if key in accepted:
                raise ValueError(f"duplicate validator result: {key}")
            accepted[key] = row
    drift = [path for path, value in report["inputs"].items()
             if changed(Path(path), value)]
    report["drift"] = sorted(set(report.get("drift", [])) | set(drift))
    for row in report["rows"]:
        reasons = list(report["drift"])
        for provider, group in row["providers"].items():
            validation = accepted.get((row["id"], row["target"], provider))
            group["targetValidation"] = validation or {"status": "not_run"}
            samples = group["samples"]
            if group["failures"]:
                reasons += [provider + ": " + reason for reason in group["failures"]]
            if not validation or validation["status"] != "passed":
                reasons.append(provider + ": target SDK acceptance absent or failed")
            elif not samples or validation["artifactSha256"] != samples[0].get("outputSha256"):
                reasons.append(provider + ": SDK receipt output differs from measured output")
        row["comparisonFailures"] = reasons
        row["comparisonStatus"] = "incomparable" if reasons else "comparable_translation_process"
        row.pop("zigOverCProcessMedianRatio", None)
        if not reasons:
            c = row["providers"]["c"]["statistics"]["p50Ns"]
            zig = row["providers"]["zig"]["statistics"]["p50Ns"]
            row["zigOverCProcessMedianRatio"] = zig / c
    report["validationReceipts"] = {str(path): digest(path) for path in receipts}
    report["status"] = "compared_with_gaps" if any(
        row["comparisonFailures"] for row in report["rows"]) else "compared"
    write_json(report_path, report)
    return int(report["status"] != "compared")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, help="Fresh measurement output directory.")
    parser.add_argument("--workloads", type=Path, default=ROOT / "bench/workloads/workloads.apple.metal.json", help="Frozen compiler corpus catalog.")
    parser.add_argument("--c-bin-dir", type=Path, default=ROOT / "runtime/c/compiler/build", help="Built C stdout emitter aliases.")
    parser.add_argument("--zig-bin-dir", type=Path, default=ROOT / "runtime/zig/zig-out/bin", help="Built Zig emitters.")
    parser.add_argument("--targets", choices=TARGETS, nargs="+", default=list(TARGETS), help="Translation targets.")
    parser.add_argument("--iterations", type=int, default=16, help="Independent measured processes per arm/case.")
    parser.add_argument("--warmup", type=int, default=1, help="Excluded fresh-process warmups per arm/case.")
    parser.add_argument("--timeout", type=float, default=20, help="Per-process deadline in seconds.")
    parser.add_argument("--validate-manifest", type=Path, help="Standalone untimed validation mode.")
    parser.add_argument("--validate-target", choices=TARGETS, help="Target checked by the current host SDK.")
    parser.add_argument("--validator", help="Actual xcrun, spirv-val or DXC executable.")
    parser.add_argument("--validation-out", type=Path, help="Fresh SDK receipt/log directory.")
    parser.add_argument("--finalize-report", type=Path, help="Bind SDK receipts to an existing measurement report.")
    parser.add_argument("--validation-receipts", type=Path, nargs="+", default=[], help="Local/copied hash-bound SDK validation receipts.")
    args = parser.parse_args()
    if args.finalize_report:
        return finalize(args.finalize_report, args.validation_receipts)
    if args.validate_manifest:
        if not args.validate_target or not args.validation_out:
            parser.error("validation requires --validate-target and --validation-out")
        return validate(args)
    if not args.out or args.iterations < 1 or args.warmup < 0 or args.timeout <= 0:
        parser.error("measurement requires --out, positive iterations/timeout, nonnegative warmup")
    return measure(args)


if __name__ == "__main__":
    raise SystemExit(main())
