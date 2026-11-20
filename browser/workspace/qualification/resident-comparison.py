#!/usr/bin/env python3
"""Pair complete Resident Process outcomes. The plan and report commands never open a browser."""

import argparse
import collections
import functools
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time

ARMS = ("stock-dawn", "stock-helper", "fawn-dawn", "fawn-doe")
PAIRS = (("stock-dawn", "stock-helper"), ("stock-dawn", "fawn-dawn"), ("fawn-dawn", "fawn-doe"))
FIXTURE = Path(__file__).resolve().parents[1] / "test/browser/canvas-review.test.js"
COUNTERS = (
    "frameHostCopyBytes",
    "cdpPixelPayloadBytes",
    "nativeHelperInputBytes",
    "nativeHashedBytes",
    "logicalTextureWriteBytes",
    "mappedTileReadbackBytes",
    "gpuMaskReadbackBytes",
    "tilePixelReadbackBytes",
    "rehashedTiles",
    "mappedTileAndMaskReadbacks",
    "digestReadbackBytes",
    "digestJobUploadBytes",
    "digestComputePasses",
    "cpuPixelReadBytes",
    "plannerCalls",
    "modelSpendUsd",
    "applicationHttpRequests",
)


def read(path):
    return json.loads(path.read_text()) if path.exists() else {}


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def make_plan(repeats, widths):
    if (
        not (1 <= repeats <= 100)
        or not widths
        or len(set(widths)) != len(widths)
        or any(not 1 <= n <= 16 for n in widths)
    ):
        raise ValueError("supply 1–100 repeats and distinct concurrency levels in 1–16")
    plan = []
    for repeat in range(repeats):
        for width in widths:
            order = ARMS[repeat % 4 :] + ARMS[: repeat % 4]
            for arm in order:
                plan.append({"arm": arm, "repeat": repeat + 1, "width": width})
    return plan


def run_id(wave, slot):
    return f"r{wave['repeat']:03}-c{wave['width']:02}-{wave['arm']}-p{slot:02}"



def chromium_gpu_process(directory: Path) -> bool:
    flag = b"--type=gpu-process"
    try:
        argv = (directory / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")
        if flag in argv[1:]:
            return True
        # Chromium on Linux can rewrite argv as one display string. Bind its
        # leading executable to /proc/exe, including spaces in the real path.
        if len(argv) == 1:
            prefix = os.fsencode((directory / "exe").readlink()) + b" " + flag
            return argv[0] == prefix or argv[0].startswith(prefix + b" ")
    except OSError:
        pass
    return False


def drm_residency(pid: int, proc: Path = Path("/proc")) -> dict | None:
    """Linux DRM fdinfo for one observed Chromium GPU subprocess."""
    directory = proc / str(pid)
    if not chromium_gpu_process(directory):
        return None
    try:
        descriptors = list((directory / "fd").iterdir())
    except OSError:
        return None

    clients = {}
    for fd in descriptors:
        try:
            if not re.fullmatch(r"/dev/dri/(?:renderD|card)\d+", str(fd.readlink())):
                continue
            info = (directory / "fdinfo" / fd.name).read_text()
        except FileNotFoundError:
            continue  # The descriptor closed between enumeration and sampling.
        except (OSError, UnicodeError):
            return None
        fields = dict(line.split(":", 1) for line in info.splitlines() if ":" in line)
        client = fields.get("drm-client-id", "").strip()
        driver = fields.get("drm-driver", "").strip()
        if not driver or not client.isdecimal():
            return None
        device = fields.get("drm-pdev", "").strip()
        key = (driver, device, client)
        if key in clients:
            continue
        regions = {}
        for prefix in (("drm-memory-", "drm-resident-") if driver == "amdgpu" else ("drm-resident-",)):
            for field, value in fields.items():
                if not field.startswith(prefix):
                    continue
                match = re.fullmatch(r"(\d+)(?:\s+(B|KiB|MiB))?", value.strip())
                if not match:
                    return None
                count, unit = match.groups()
                regions[field[len(prefix):]] = int(count) * {None: 1, "B": 1, "KiB": 1024, "MiB": 1024**2}[unit]
        if not regions:
            return None
        clients[key] = regions
    if not clients:
        return None
    devices = {}
    for (driver, device, _), regions in clients.items():
        buckets = devices.setdefault(f"{driver}@{device}" if device else driver, {})
        for region, size in regions.items():
            buckets[region] = buckets.get(region, 0) + size
    return {
        "bytes": sum(sum(regions.values()) for regions in devices.values()),
        "byDeviceAndRegion": devices,
        "clients": len(clients),
    }


def sample_tree_rss(jobs):
    # One snapshot for the entire wave. RSS sums may count shared mappings twice.
    out = subprocess.run(
        ["ps", "-axo", "pid=,ppid=,rss="], capture_output=True, text=True, timeout=5, check=True
    )
    by_parent = collections.defaultdict(list)
    sizes = {}
    for line in out.stdout.splitlines():
        pid, parent, kib = map(int, line.split())
        by_parent[parent].append(pid)
        sizes[pid] = kib * 1024
    for job in jobs:
        root = job["child"].pid
        if root not in sizes:
            continue
        gpu_pid = job.get("gpuPid")
        stack, total = [root], 0
        while stack:
            pid = stack.pop()
            if gpu_pid is not None and pid == gpu_pid:
                observed = drm_residency(pid)
                job["drmAttempts"] += 1
                if observed is not None:
                    job["drmSamples"] += 1
                    if job["drmPeak"] is None or observed["bytes"] > job["drmPeak"]["bytes"]:
                        job["drmPeak"] = observed
            total += sizes[pid]
            stack.extend(by_parent[pid])
        job["samples"] += 1
        job["peak"] = max(job["peak"], total)


def git_revision(value):
    if not isinstance(value, str):
        return None
    value = value.removeprefix("@")
    return value if re.fullmatch(r"[0-9a-f]{40}", value) and value != "0" * 40 else None


def source_receipt(path):
    value = read(path)
    if not isinstance(value, dict):
        raise ValueError(f"Invalid pinned browser build receipt: {path}")
    fields = ("chromium", "dawn", "chromeSha256", "source", "product")
    if not all(isinstance(value.get(k), str) and value[k].strip() for k in fields):
        raise ValueError(f"Incomplete pinned browser build receipt: {path}")
    for key in ("chromium", "dawn"):
        if not git_revision(value[key]) or value[key].startswith("@"):
            raise ValueError(f"Invalid {key} revision in browser build receipt: {path}")
    if not re.fullmatch(r"[0-9a-f]{64}", value["chromeSha256"]):
        raise ValueError(f"Invalid executable digest in browser build receipt: {path}")
    return value


@functools.lru_cache(maxsize=8)
def image_digest(path, stamp):
    # Hash one immutable executable once across the cohort, before the measurement barrier.
    with open(path, "rb") as image:
        if file_stamp(os.fstat(image.fileno())) != stamp:
            raise ValueError("Browser image changed before hashing")
        digest = hashlib.sha256()
        for block in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(block)
        if file_stamp(os.fstat(image.fileno())) != stamp:
            raise ValueError("Browser image changed during hashing")
    return digest.hexdigest()


def file_stamp(stat):
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns


def source_for_browser(progress, receipt, owner_pid):
    """Match a browser returned by DevTools to its fixture-owned Linux process and pinned image."""
    if not receipt:
        return None
    pid = progress.get("browserPid")
    if type(pid) is not int or pid <= 1 or pid == owner_pid:
        raise ValueError("No unique browser process")
    proc = Path("/proc") / str(pid)
    # The root returned by CDP must be a descendant of this exact test process.
    ancestor = pid
    while ancestor > 1 and ancestor != owner_pid:
        status = (Path("/proc") / str(ancestor) / "status").read_text().splitlines()
        ancestor = int(next(line for line in status if line.startswith("PPid:")).split()[1])
    if ancestor != owner_pid:
        raise ValueError("Browser process belongs to a different fixture")
    live = proc / "exe"
    stamp = file_stamp(live.stat())
    actual = image_digest(live.resolve(strict=True), stamp)
    version = progress["browser"]["product"].partition("/")[2]
    cdp = git_revision(progress["browser"].get("revision"))
    if (actual != receipt["chromeSha256"] or file_stamp(live.stat()) != stamp
            or version != receipt["product"].split()[-1] or (cdp and cdp != receipt["chromium"])):
        raise ValueError("Observed browser differs from its pinned build")
    return {k: receipt[k] for k in ("chromeSha256", "chromium", "dawn", "source", "product")}


def comparable_browsers(a, b):
    av, bv = a.get("browser") or {}, b.get("browser") or {}
    version = (av.get("product") or "").partition("/")[2]
    if not version or version != (bv.get("product") or "").partition("/")[2]:
        return False
    left, right = a.get("browserSource"), b.get("browserSource")
    if left or right:
        return bool(left and right and left["chromium"] == right["chromium"] and left["dawn"] == right["dawn"])
    revision = git_revision(av.get("revision"))
    return bool(revision and revision == git_revision(bv.get("revision")))


def run_wave(out, wave, args):
    gate = out / (run_id(wave, 0) + ".go")
    sources = getattr(args, "sources", {})
    jobs, timeout, admitted = [], False, False
    started = time.monotonic()
    try:
        for slot in range(1, wave["width"] + 1):
            row = out / run_id(wave, slot)
            row.mkdir()
            env = {
                k: v
                for k, v in os.environ.items()
                if not k.startswith(("BLITZEN_TEST_", "FAWN_WORKSPACE_TEST_"))
            }
            if wave["arm"].startswith("fawn"):
                env["BLITZEN_TEST_BROWSER_CONFIG"] = str(
                    args.doe if wave["arm"] == "fawn-doe" else args.dawn
                )
            else:
                env["BLITZEN_TEST_BROWSER_CHANNEL"] = args.stock_channel
            if sources or sys.platform.startswith("linux"):
                env["BLITZEN_RESIDENT_OBSERVE_BROWSER_PID"] = "1"
            env.update(
                BLITZEN_TEST_ELEMENT_CAPTURE="1",
                BLITZEN_TEST_COMPOSITOR_DIGEST="cpu" if wave["arm"] == "stock-helper" else args.compositor_digest,
                BLITZEN_RESIDENT_PHYSICAL="1",
                BLITZEN_REVIEW_COMPOSITOR_TRANSPORT="native-helper"
                if wave["arm"] == "stock-helper"
                else "external-texture",
                BLITZEN_RESIDENT_RECEIPT_FILE=str(row / "outcome.json"),
                BLITZEN_RESIDENT_PROGRESS_FILE=str(row / "progress.json"),
                BLITZEN_RESIDENT_BARRIER_FILE=str(gate),
            )
            with (row / "fixture.log").open("wb") as log:
                child = subprocess.Popen(
                    ["node", str(FIXTURE)], env=env, stdout=log, stderr=subprocess.STDOUT
                )
            jobs.append({"dir": row, "child": child, "peak": 0, "samples": 0,
                         "drmPeak": None, "drmSamples": 0, "drmAttempts": 0})
        while any(j["child"].poll() is None for j in jobs):
            if not admitted:
                phases = [read(j["dir"] / "progress.json").get("phase") for j in jobs]
                if all(p == "ready" for p in phases):
                    if sources:
                        source = sources["fawn" if wave["arm"].startswith("fawn") else "stock"]
                        for job in jobs:
                            progress = read(job["dir"] / "progress.json")
                            observed = source_for_browser(progress, source, job["child"].pid)
                            write(job["dir"] / "browser-source.json", observed)
                    if sys.platform.startswith("linux"):
                        for job in jobs:
                            job["gpuPid"] = read(job["dir"] / "progress.json").get("gpuProcessPid")
                    gate.touch(exist_ok=False)
                    admitted = True
                elif any(j["child"].poll() is not None for j in jobs):
                    break  # Other workers may have independent grants. Do not time a smaller cohort.
            sample_tree_rss(jobs)
            if time.monotonic() - started > args.timeout:
                timeout = True
                break
            time.sleep(0.25)
    finally:
        for job in jobs:
            child = job["child"]
            if child.poll() is None:
                child.terminate()
        for job in jobs:
            child = job["child"]
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            write(
                job["dir"] / "exit.json",
                {
                    "exit": child.returncode,
                    "allReady": admitted,
                    "waveTimeout": timeout,
                    "peakSampledTreeRssBytes": job["peak"] if job["samples"] else None,
                    "rssSamples": job["samples"],
                    "physicalGpuMemoryBytes": None,
                    "gpuProcessDrm": {"peak": job["drmPeak"], "samples": job["drmSamples"],
                                      "attempts": job["drmAttempts"]},
                },
            )


def summarize_row(path, wave, slot):
    p, e, o = (read(path / file) for file in ("progress.json", "exit.json", "outcome.json"))
    data = {
        **wave,
        "slot": slot,
        "status": "missing",
        "reason": None,
        "imageCorrect": o.get("imageCorrect", p.get("imageCorrect")),
        "indicator": o.get("indicator", p.get("indicator")),
        "knownEffects": o.get("knownEffects", p.get("knownEffects")),
        "committedRevision": o.get("committedRevision"),
        "requestMonotonicMs": o.get("requestMonotonicMs", p.get("requestMonotonicMs")),
        "mutationReturnedMonotonicMs": o.get(
            "mutationReturnedMonotonicMs", p.get("mutationReturnedMonotonicMs")
        ),
        "browserMutationEpochMs": o.get("browserMutationEpochMs", p.get("browserMutationEpochMs")),
        "outcomeVerifiedMonotonicMs": o.get(
            "outcomeVerifiedMonotonicMs", p.get("outcomeVerifiedMonotonicMs")
        ),
        "nodeTimeOriginMs": p.get("nodeTimeOriginMs"),
        "browser": p.get("browser"),
        "browserSource": read(path / "browser-source.json") or None,
        "compositorDigest": o.get("digestMode"),
        "gpu": p.get("gpu"),
        "surfaceId": p.get("surfaceId"),
        "targetId": p.get("targetId"),
        "commandLabel": p.get("commandLabel"),
        "selection": p.get("selection"),
        "pageChangeToVerifiedMidpointMs": None,
        "pageChangeToVerifiedBoundsMs": None,
        "peakSampledTreeRssBytes": e.get("peakSampledTreeRssBytes"),
        "rssSamples": e.get("rssSamples"),
        "physicalGpuMemoryBytes": None,
        "gpuProcessDrm": e.get("gpuProcessDrm"),
        **{k: o.get(k, p.get("partial", {}).get(k)) for k in COUNTERS},
    }
    if not e:
        data["reason"] = "no completed runner record"
        return data
    if p.get("failureType") == "AssertionError":
        data.update(status="incorrect", reason=p.get("failure") or "fixture oracle rejected output")
    elif e.get("waveTimeout"):
        data.update(status="timeout", reason=p.get("failure") or "cohort exceeded its deadline")
    elif e["exit"] != 0 or not e["allReady"] or not o:
        data.update(
            status="refused",
            reason=p.get("failure") or f"exit {e['exit']}, full cohort ready {e['allReady']}",
        )
    elif not (
        o.get("verified")
        and data["imageCorrect"]
        and data["knownEffects"] == 1
        and data["committedRevision"] == "B-r2"
    ):
        data.update(
            status="incorrect", reason="image, application revision, or unique effect did not verify"
        )
    elif o.get("diagnosticOnly") is True:
        data.update(status="unqualified", reason="capture-authority diagnostic adds track observations and a fence wait")
    elif wave["arm"] == "fawn-doe":
        # A native producer must bind this target's active capture and diff command
        # to completed work on the loaded Doe device. Flags, adapter strings or a
        # neighboring page's marker cannot discharge this gate.
        data.update(
            status="unattributed",
            reason="independent native page dispatch and completion proof unavailable",
        )
    elif wave["arm"] != "stock-helper" and not hardware_adapter(data.get("gpu")):
        data.update(status="unqualified", reason="working hardware adapter unconfirmed")
    else:
        start, changed, finish = (
            data[k]
            for k in ("requestMonotonicMs", "mutationReturnedMonotonicMs", "outcomeVerifiedMonotonicMs")
        )
        if (
            not all(isinstance(n, (float, int)) and math.isfinite(n) for n in (start, changed, finish))
            or not start <= changed <= finish
        ):
            data.update(status="unqualified", reason="missing or inconsistent monotonic phase stamps")
        else:
            low, high = finish - changed, finish - start
            data.update(
                status="verified",
                pageChangeToVerifiedMidpointMs=(low + high) / 2,
                pageChangeToVerifiedBoundsMs=[low, high],
            )
    if wave["arm"] == "fawn-doe":
        # No Doe timings are published, even as reconstructible phase timestamps.
        for k in (
            "requestMonotonicMs",
            "mutationReturnedMonotonicMs",
            "outcomeVerifiedMonotonicMs",
            "browserMutationEpochMs",
        ):
            data[k] = None
    return data


def hardware_adapter(info):
    if not info or info.get("isFallbackAdapter") is not False:
        return False
    text = " ".join(
        str(info.get(k) or "") for k in ("vendor", "architecture", "device", "description")
    ).lower()
    return bool(text.strip()) and not any(
        s in text for s in ("swiftshader", "lavapipe", "llvmpipe", "software", "warp")
    )


def stats(values):
    if not values:
        return {"n": 0, "p50Ms": None, "p95Ms": None}
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "p50Ms": statistics.median(ordered),
        "p95Ms": ordered[math.ceil(0.95 * len(ordered)) - 1],
    }


def summarize(out):
    plan = read(out / "plan.json")
    if not isinstance(plan, list) or not plan:
        raise ValueError("missing frozen comparison plan")
    rows = [summarize_row(out / run_id(w, s), w, s) for w in plan for s in range(1, w["width"] + 1)]
    # An incomplete concurrency wave is still retained, but cannot establish matched load.
    waves = collections.defaultdict(list)
    for row in rows:
        waves[row["width"], row["repeat"], row["arm"]].append(row)
    for wave_rows in waves.values():
        if len(wave_rows) < 2 or any(r["status"] != "verified" for r in wave_rows):
            overlap = len(wave_rows) == 1 and wave_rows[0]["status"] == "verified"
        else:
            overlap = all(isinstance(r["nodeTimeOriginMs"], (int, float)) for r in wave_rows) and max(
                r["nodeTimeOriginMs"] + r["mutationReturnedMonotonicMs"] for r in wave_rows
            ) <= min(r["nodeTimeOriginMs"] + r["outcomeVerifiedMonotonicMs"] for r in wave_rows)
        for row in wave_rows:
            row["fullMeasuredOverlap"] = overlap
    by_arm, pairs = [], []
    for width in sorted({w["width"] for w in plan}):
        for arm in ARMS:
            items = [r for r in rows if r["width"] == width and r["arm"] == arm]
            samples = [r for r in items if r["status"] == "verified" and r["fullMeasuredOverlap"]]
            by_arm.append(
                {
                    "arm": arm,
                    "concurrency": width,
                    "planned": len(items),
                    "statuses": dict(collections.Counter(r["status"] for r in items)),
                    "excludedIncompleteWaves": sum(
                        r["status"] == "verified" and not r["fullMeasuredOverlap"] for r in items
                    ),
                    **stats([r["pageChangeToVerifiedMidpointMs"] for r in samples]),
                }
            )
        for left, right in PAIRS:
            deltas, skipped = [], collections.Counter()
            for w in (w for w in plan if w["arm"] == left and w["width"] == width):
                aa, bb = waves[width, w["repeat"], left], waves[width, w["repeat"], right]
                for a, b in zip(aa, bb, strict=True):
                    if (
                        a["status"] != "verified"
                        or b["status"] != "verified"
                        or not a["fullMeasuredOverlap"]
                        or not b["fullMeasuredOverlap"]
                    ):
                        skipped["incomplete, unattributed or incorrect"] += 1
                        continue

                    if not comparable_browsers(a, b):
                        skipped["browser source/version unmatched"] += 1
                        continue
                    deltas.append(
                        b["pageChangeToVerifiedMidpointMs"] - a["pageChangeToVerifiedMidpointMs"]
                    )
            pairs.append(
                {
                    "baseline": left,
                    "candidate": right,
                    "concurrency": width,
                    "omitted": dict(skipped),
                    **stats(deltas),
                }
            )
    return {
        "clock": "Host monotonic bounds bracket the page canvas mutation, ending at authoritative application reconciliation. Midpoint is an estimate. No compositor frame-commit stamp.",
        "aggregation": "Nearest-rank p95 per completed process, median p50. Paired values are candidate minus baseline milliseconds in matching repeat and slot. Report every planned attempt; incomplete overlapping cohorts excluded.",
        "memoryScope": "Process-tree RSS is sampled about every 250 ms. From browser readiness, Linux DRM counts resident bytes reported for the fixture GPU process. The peak contains one sample across regions, deduplicated by device and DRM client. Separate clients can count shared buffers twice. Missing samples are unknown. Whole-device VRAM is unmeasured.",
        "attribution": "Matching source revisions and versions permits a whole-browser comparison only. Build options and renderer paths may differ. Fawn/Doe timings stay withheld pending native per-page provenance.",
        "byArm": by_arm,
        "paired": pairs,
        "rows": rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="op", required=True)
    for command in ("plan", "run", "report"):
        p = sub.add_parser(command)
        p.add_argument("--out", type=Path, required=True)
        if command == "plan":
            p.add_argument("--repeats", type=int, default=20)
            p.add_argument("--concurrency", type=int, nargs="+", default=[1, 2])
        if command == "run":
            p.add_argument("--dawn", type=Path, required=True)
            p.add_argument("--doe", type=Path, required=True)
            p.add_argument("--source-receipts", nargs=2, type=Path, metavar=("STOCK", "FAWN"),
                           help="bind both Linux browser images to their operator-verified source receipts")
            p.add_argument("--compositor-digest", choices=["cpu", "webgpu"], default="cpu",
                           help="digest for the three browser GPU arms; the native helper retains its own CPU digest")
            p.add_argument("--stock-channel", choices=["chrome", "chromium"], default="chrome")
            p.add_argument("--timeout", type=int, default=175)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.op == "plan":
        plan = make_plan(args.repeats, args.concurrency)
        out.mkdir(exist_ok=False, parents=True)
        write(out / "plan.json", plan)
    elif args.op == "run":
        if sys.platform not in ("darwin", "linux"):
            parser.error("physical resource sampling supports Linux and macOS")
        if os.environ.get("NODE_OPTIONS"):
            parser.error("run with NODE_OPTIONS unset to preserve the browser launch")
        if not 1 <= args.timeout <= 600:
            parser.error("choose a 1–600 second cohort timeout")
        args.dawn, args.doe = args.dawn.resolve(), args.doe.resolve()
        d, c = (read(p)["browser"]["fawnBuild"] for p in (args.dawn, args.doe))
        if (
            d["runtime"]["mode"] != "dawn"
            or c["runtime"]["mode"] != "doe-c"
            or d["executable"] != c["executable"]
            or d["dependencies"] != c["dependencies"]
        ):
            parser.error(
                "both profiles must pin the identical Fawn executable/dependencies and force their declared runtime"
            )
        args.sources = {}
        if args.source_receipts:
            if sys.platform != "linux":
                parser.error("source receipt matching requires Linux process image handles")
            args.sources = dict(zip(("stock", "fawn"), map(source_receipt, args.source_receipts), strict=True))
            if args.sources["fawn"]["chromeSha256"] != d["executable"]["sha256"]:
                parser.error("Fawn source receipt differs from the selected build")
        plan = read(out / "plan.json")
        if not plan:
            parser.error("create the plan first")
        with (out / "run-started").open("x") as f:
            f.write(str(os.getpid()))
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
        for wave in plan:
            print(wave, flush=True)
            run_wave(out, wave, args)
        write(out / "report.json", summarize(out))
    else:
        write(out / "report.json", summarize(out))
        print(out / "report.json")


if __name__ == "__main__":
    main()
