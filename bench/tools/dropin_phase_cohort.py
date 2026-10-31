"""Balanced phase-attributed provider cohorts over bench/drop-in/dropin_phase_harness.c.

One harness source is compiled once per provider library (Doe C, retained Doe
Zig, Dawn). ``prepare`` freezes inputs and independent CPU oracles, ``build``
links the harness, ``run`` executes balanced provider orders per pair and
retains every raw process result, ``summarize`` reports per-phase medians, p95,
paired ratios with bootstrap confidence intervals and sample counts.

Timing is a host monotonic clock per phase (CLOCK_MONOTONIC; CLOCK_UPTIME_RAW on
Apple; QueryPerformanceCounter on Windows), not GPU timestamps. ``build --backend`` fixes the adapter backend the
harness requests and asserts (Vulkan by default, or Metal). The oracle check
runs after each iteration's host-operation interval closes and is mandatory.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
import random
import statistics
import struct
import subprocess
import sys
import time
from pathlib import Path

from bench.lib import synthetic_assets

if os.name == "nt":
    import ctypes
    from ctypes import wintypes
else:
    import resource

REPO = Path(__file__).resolve().parents[2]
HARNESS = REPO / "bench/drop-in/dropin_phase_harness.c"
HEADERS = REPO / "runtime/zig/vendor/webgpu-headers"
# Node5 package workloads with frozen generator inputs. Set 0 is the frozen
# Node5 asset; set 1 uses the same generator with a distinct seed so stale
# output from the previous iteration cannot satisfy the oracle.
WORKLOADS = {
    "vector_scale_add_262k": {
        "plan": "bench/plans/package-developer/package_vector_scale_add_262k.plan.json",
        "shader": "bench/kernels/package_vector_scale_add.wgsl",
        "seed": 211, "scale": 0.125, "bytes": 1048576, "kind": "scale_add",
    },
    "image_rgba_invert_1024": {
        "plan": "bench/plans/package-developer/package_image_rgba_invert_1024.plan.json",
        "shader": "bench/kernels/package_image_rgba_invert.wgsl",
        "seed": 307, "scale": 0.125, "bytes": 4194304, "kind": "invert",
    },
    "buffer_upload_readback_exact_1mb": {
        "plan": "bench/plans/package-developer/package_buffer_upload_readback_exact_1mb.plan.json",
        "shader": None,
        "seed": 101, "scale": 0.125, "bytes": 1048576, "kind": "copy",
    },
}
SECOND_SET_SEED_OFFSET = 1000
ONE_TIME = ("setupNs", "setupInstanceNs", "setupAdapterNs", "setupDeviceNs", "compileNs", "pipelineNs", "resourcesNs", "teardownNs", "teardownReleaseNs", "teardownDeviceNs", "processInMainNs")
LOOP_PHASES = ("upload", "encode", "submit", "completion", "map", "operation")
BOOTSTRAP_RESAMPLES = 10000
BOOTSTRAP_SEED = 20261029


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def oracle(kind: str, data: bytes) -> tuple[bytes, bytes]:
    """Independent CPU reference; returns (A, B) where B differs only by fused rounding."""
    if kind == "copy":
        return data, data
    words = len(data) // 4
    if kind == "invert":
        values = struct.unpack(f"<{words}I", data)
        out = struct.pack(f"<{words}I", *(v ^ 0x00FFFFFF for v in values))
        return out, out
    if kind == "scale_add":
        values = struct.unpack(f"<{words}f", data)
        separate = [f32(f32(v * 1.5) + 0.25) for v in values]
        fused = [f32(v * 1.5 + 0.25) for v in values]  # exact in binary64 for these inputs
        return struct.pack(f"<{words}f", *separate), struct.pack(f"<{words}f", *fused)
    raise ValueError(f"unknown oracle kind {kind}")


def cross_check_frozen_plan(name: str, spec: dict, a: bytes, b: bytes, oracle_plans: Path | None) -> str:
    """Set 0 must agree with the frozen Node5 capture oracle (plan or prepared CPU-oracle plan)."""
    plan = json.loads((REPO / spec["plan"]).read_text())
    validate = next((c.get("captureValidate") for c in plan["commands"] if c.get("captureValidate")), None)
    if validate is None:
        prepared = oracle_plans / f"package_{name}.cpu-oracle-v1.plan.json" if oracle_plans else None
        if prepared is None or not prepared.exists():
            raise AssertionError(f"{name}: no frozen capture oracle; pass --oracle-plans from prepare_package_oracle_plans")
        plan = json.loads(prepared.read_text())
        validate = next(c["captureValidate"] for c in plan["commands"] if c.get("captureValidate"))
    if validate["kind"] == "sha256Equals":
        if hashlib.sha256(a).hexdigest() != validate["sha256"] or a != b:
            raise AssertionError(f"{name}: independent oracle disagrees with frozen sha256")
        return "sha256Equals"
    if validate["kind"] == "f32BufferOneOfV1":
        words_a = struct.unpack(f"<{len(a) // 4}f", a)
        words_b = struct.unpack(f"<{len(b) // 4}f", b)
        alternatives = validate["values"]
        if len(alternatives) != len(words_a):
            raise AssertionError(f"{name}: frozen oracle length {len(alternatives)} != {len(words_a)}")
        for index, allowed in enumerate(alternatives):
            allowed32 = {f32(v) for v in allowed}
            if not {words_a[index], words_b[index]} <= allowed32:
                raise AssertionError(f"{name}: word {index} outside frozen alternatives")
        return "f32BufferOneOfV1"
    raise AssertionError(f"{name}: unsupported frozen oracle {validate['kind']}")


def prepare(out: Path, oracle_plans: Path | None) -> None:
    out.mkdir(parents=True, exist_ok=False)
    manifest = {"schemaVersion": 1, "workloads": {}}
    for name, spec in WORKLOADS.items():
        row = {"kind": spec["kind"], "bytes": spec["bytes"], "sets": []}
        if spec["shader"]:
            row["shader"] = spec["shader"]
            row["shaderSha256"] = sha256(REPO / spec["shader"])
        for index, seed in enumerate((spec["seed"], spec["seed"] + SECOND_SET_SEED_OFFSET)):
            data = synthetic_assets.generate_asset_bytes(
                generator="splitmix64_f32_nonzero_v1", seed=seed, scale=spec["scale"], byte_length=spec["bytes"])
            a, b = oracle(spec["kind"], data)
            paths = {}
            for label, payload in (("input", data), ("expectA", a), ("expectB", b)):
                path = out / f"{name}.set{index}.{label}.bin"
                path.write_bytes(payload)
                paths[label] = {"path": path.name, "sha256": hashlib.sha256(payload).hexdigest()}
            check = cross_check_frozen_plan(name, spec, a, b, oracle_plans) if index == 0 else "same-generator-distinct-seed"
            row["sets"].append({"seed": seed, "frozenCrossCheck": check, **paths})
        manifest["workloads"][name] = row
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"status": "prepared", "out": str(out)}))


BACKEND_DEFINES = {"vulkan": [], "metal": ["-DDOE_PHASE_BACKEND_METAL"]}


def build(library: Path, output: Path, compiler: str, backend: str) -> None:
    if os.name == "nt":
        # clang-cl links the provider's import library. The DLL must sit beside the
        # harness: Windows resolves it from the executable directory first, so no
        # PATH or working-directory search can substitute another provider.
        imports = library.with_suffix(".lib")
        if output.resolve().parent != library.resolve().parent or not imports.is_file():
            raise SystemExit("Windows build: --output must be beside the DLL, with its import library")
        argv = [compiler, "--target=x86_64-pc-windows-msvc", "/nologo", "/O2", "/MD", "/WX", "/clang:-std=c11",
                "/clang:-Wall", "/clang:-Wextra", "/D_CRT_SECURE_NO_WARNINGS", *BACKEND_DEFINES[backend],
                f"/I{HEADERS}", str(HARNESS), f"/Fe{output}", f"/Fo{output.with_suffix('.obj')}",
                "/link", str(imports.resolve()), "psapi.lib"]
    else:
        argv = [compiler, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", *BACKEND_DEFINES[backend], f"-I{HEADERS}",
                str(HARNESS), str(library.resolve()), f"-Wl,-rpath,{library.resolve().parent}", "-o", str(output)]
    subprocess.run(argv, check=True)
    print(json.dumps({"binary": str(output), "sha256": sha256(output), "library": str(library), "backend": backend,
                      "librarySha256": sha256(library), "harnessSha256": sha256(HARNESS), "argv": argv}))


def run_process(argv: list[str], timeout: float) -> tuple[int, str, str, int, float]:
    """Returns (code, stdout, stderr, wall ns, child CPU ms)."""
    if os.name != "nt":
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic_ns()
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        wall = time.monotonic_ns() - start
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = ((after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)) * 1e3
        return result.returncode, result.stdout, result.stderr, wall, cpu
    start = time.monotonic_ns()
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            raise
        wall = time.monotonic_ns() - start
        # The exited process handle stays open until Popen closes it.
        times = [wintypes.FILETIME() for _ in range(4)]
        if not ctypes.WinDLL("kernel32", use_last_error=True).GetProcessTimes(wintypes.HANDLE(int(process._handle)), *map(ctypes.byref, times)):
            raise OSError(ctypes.get_last_error(), "GetProcessTimes")
        ticks = [(t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]]
        return process.returncode, stdout, stderr, wall, sum(ticks) / 1e4


def harness_args(inputs: Path, name: str, iterations: int) -> list[str]:
    row = json.loads((inputs / "manifest.json").read_text())["workloads"][name]
    shader = str(REPO / row["shader"]) if row.get("shader") else "-"
    sets = row["sets"]
    files = [sets[0]["input"], sets[1]["input"], sets[0]["expectA"], sets[0]["expectB"], sets[1]["expectA"], sets[1]["expectB"]]
    for entry in files:
        if sha256(inputs / entry["path"]) != entry["sha256"]:
            raise AssertionError(f"frozen input changed: {entry['path']}")
    return [shader, *(str(inputs / entry["path"]) for entry in files), str(iterations)]


def run(args: argparse.Namespace) -> None:
    providers = dict(item.split("=", 1) for item in args.provider)
    names = sorted(providers)
    orders = list(itertools.permutations(names))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    workloads = args.workload or list(WORKLOADS)
    failures = 0
    with out.open("x") as stream:
        for workload in workloads:
            argv_tail = harness_args(Path(args.inputs), workload, args.iterations)
            for pair in range(args.pairs):
                order = orders[pair % len(orders)]
                for position, provider in enumerate(order):
                    code, stdout, stderr, wall, cpu = run_process([providers[provider], *argv_tail], args.timeout)
                    try:
                        payload = json.loads(stdout.strip().splitlines()[-1])
                    except (IndexError, ValueError):
                        payload = {"status": "failed", "error": "unparseable_stdout"}
                    if code != 0 or payload.get("status") != "ok":
                        failures += 1
                    row = {"workload": workload, "pair": pair, "position": position, "order": list(order),
                           "provider": provider, "binary": providers[provider], "returnCode": code,
                           "processWallNs": wall, "childCpuMs": cpu, "stderr": stderr[-2000:], "result": payload}
                    stream.write(json.dumps(row) + "\n")
                    stream.flush()
    print(json.dumps({"status": "complete" if failures == 0 else "failures", "failures": failures, "out": str(out)}))
    if failures:
        sys.exit(1)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))]


def bootstrap_median_ci(values: list[float]) -> tuple[float, float]:
    rng = random.Random(BOOTSTRAP_SEED)
    medians = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(BOOTSTRAP_RESAMPLES))
    return medians[int(0.025 * BOOTSTRAP_RESAMPLES)], medians[int(0.975 * BOOTSTRAP_RESAMPLES) - 1]


def process_metrics(row: dict) -> dict[str, float]:
    result = row["result"]
    metrics = {key: result[key] / 1e6 for key in ONE_TIME if key in result}
    # Complete operation after device acquisition: one-time preparation, every
    # iteration's host-operation interval (CPU oracle excluded) and teardown.
    if all(key in result for key in ("compileNs", "pipelineNs", "resourcesNs", "teardownNs")):
        metrics["complete"] = (result["compileNs"] + result["pipelineNs"] + result["resourcesNs"] +
                               sum(result["phases"]["operation"]) + result["teardownNs"]) / 1e6
    metrics["processWall"] = row["processWallNs"] / 1e6
    metrics["cpuMs"] = row["childCpuMs"]
    metrics["maxRssMiB"] = result["maxRssKiB"] / 1024
    for phase in LOOP_PHASES:
        series = result["phases"][phase]
        metrics[f"first.{phase}"] = series[0] / 1e6
        warm = series[1:] or series
        metrics[f"warm.{phase}"] = statistics.median(warm) / 1e6
        metrics[f"warmP95.{phase}"] = percentile(warm, 0.95) / 1e6
    return metrics


def summarize(args: argparse.Namespace) -> None:
    rows = [json.loads(line) for path in args.raw for line in Path(path).read_text().splitlines() if line.strip()]
    failed = [r for r in rows if r["returnCode"] != 0 or r["result"].get("status") != "ok"]
    ok = [r for r in rows if r not in failed]
    report = {"schemaVersion": 1, "rawFiles": {p: sha256(Path(p)) for p in args.raw}, "failedProcesses": len(failed),
              "bootstrap": {"resamples": BOOTSTRAP_RESAMPLES, "seed": BOOTSTRAP_SEED, "interval": 0.95},
              "timing": "host monotonic clock per phase (CLOCK_MONOTONIC; CLOCK_UPTIME_RAW on Apple; QueryPerformanceCounter on Windows); not GPU timestamps",
              "workloads": {}}
    for workload in sorted({r["workload"] for r in rows}):
        by_pair: dict[int, dict[str, dict]] = {}
        for r in ok:
            if r["workload"] == workload:
                by_pair.setdefault(r["pair"], {})[r["provider"]] = process_metrics(r)
        providers = sorted({p for pair in by_pair.values() for p in pair})
        metrics = sorted(next(iter(next(iter(by_pair.values())).values())).keys()) if by_pair else []
        section = {"providers": {}, "paired": {}}
        for provider in providers:
            per = [pair[provider] for pair in by_pair.values() if provider in pair]
            pooled = {phase: [v / 1e6 for r in ok if r["workload"] == workload and r["provider"] == provider
                              for v in r["result"]["phases"][phase][1:]] for phase in LOOP_PHASES}
            section["providers"][provider] = {
                "processes": len(per),
                "warmIterationsPooled": len(pooled["operation"]),
                "median": {m: statistics.median(p[m] for p in per) for m in metrics},
                "p95AcrossProcesses": {m: percentile([p[m] for p in per], 0.95) for m in metrics},
                "pooledWarmP95": {phase: percentile(values, 0.95) for phase, values in pooled.items() if values},
                "pooledWarmMedian": {phase: statistics.median(values) for phase, values in pooled.items() if values},
            }
        control = args.control
        for provider in providers:
            if provider == control:
                continue
            pairs = [pair for pair in by_pair.values() if provider in pair and control in pair]
            paired = {"pairs": len(pairs), "ratio": {}}
            for m in metrics:
                ratios = [pair[provider][m] / pair[control][m] for pair in pairs if pair[control][m] > 0]
                if len(ratios) < 2:
                    continue
                low, high = bootstrap_median_ci(ratios)
                paired["ratio"][m] = {"median": statistics.median(ratios), "ci95": [low, high], "n": len(ratios),
                                      "providerFasterPairs": sum(1 for x in ratios if x < 1)}
            section["paired"][f"{provider}/{control}"] = paired
        report["workloads"][workload] = section
    text = json.dumps(report, indent=2) + "\n"
    if args.out:
        Path(args.out).write_text(text)
    print(text)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--out", required=True)
    p.add_argument("--oracle-plans", help="directory from python3 -m bench.tools.prepare_package_oracle_plans")
    b = sub.add_parser("build")
    b.add_argument("--library", required=True)
    b.add_argument("--output", required=True)
    b.add_argument("--cc", default="gcc")
    b.add_argument("--backend", choices=sorted(BACKEND_DEFINES), default="vulkan")
    r = sub.add_parser("run")
    r.add_argument("--inputs", required=True)
    r.add_argument("--provider", action="append", required=True, help="name=harness-binary")
    r.add_argument("--workload", action="append", choices=sorted(WORKLOADS))
    r.add_argument("--pairs", type=int, default=16)
    r.add_argument("--iterations", type=int, default=32)
    r.add_argument("--timeout", type=float, default=120)
    r.add_argument("--out", required=True)
    s = sub.add_parser("summarize")
    s.add_argument("--raw", action="append", required=True)
    s.add_argument("--control", default="dawn")
    s.add_argument("--out")
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(Path(args.out), Path(args.oracle_plans) if args.oracle_plans else None)
    elif args.command == "build":
        build(Path(args.library), Path(args.output), args.cc, args.backend)
    elif args.command == "run":
        run(args)
    else:
        summarize(args)


if __name__ == "__main__":
    main()
