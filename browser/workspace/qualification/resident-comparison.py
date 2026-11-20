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
import runpy
import signal
import statistics
import subprocess
import sys
import time

ARMS = ("stock-dawn", "stock-helper", "fawn-dawn", "fawn-doe")
PAIRS = (("stock-dawn", "stock-helper"), ("stock-dawn", "fawn-dawn"), ("fawn-dawn", "fawn-doe"))
PLANE_ARM = "stock-js-i420"
CPU_ARMS = ("stock-helper", PLANE_ARM)
SPARSE_ARMS = (*ARMS, PLANE_ARM)
FIXTURE = Path(__file__).resolve().parents[1] / "test/browser/canvas-review.test.js"
COUNTERS = (
    "permissionRequests",
    "stockPlaneExportBytes",
    "stockCanonicalRgbaCopyBytes",
    "nativePlaneSnapshotCopyBytes",
    "nativePlaneSnapshotSamples",
    "stockPlaneSparseFrames",
    "stockPlaneEqualFrames",
    "sparseCanonicalFrames",
    "nativePlaneDigestReuses",
    "frameHostCopyBytes",
    "canonicalHashedBytes",
    "canonicalRehashedTiles",
    "externalHintMissedChanges",
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


def make_plan(repeats, widths, paired_observer=False, sparse_controls=False):
    if (
        not (1 <= repeats <= 100)
        or not widths
        or len(set(widths)) != len(widths)
        or any(not 1 <= n <= 16 for n in widths)
    ):
        raise ValueError("supply 1–100 repeats and distinct concurrency levels in 1–16")
    arms = SPARSE_ARMS if sparse_controls else ARMS
    plan = [{"arm": arm, "repeat": 0, "width": 1, "sparseControls": True, "oracle": True}
            for arm in ("fawn-dawn", "fawn-doe")] if sparse_controls else []
    for repeat in range(repeats):
        for width in widths:
            order = arms[repeat % len(arms):] + arms[:repeat % len(arms)]
            for arm in order:
                wave = {"arm": arm, "repeat": repeat + 1, "width": width}
                if sparse_controls:
                    wave["sparseControls"] = True
                if paired_observer or sparse_controls:
                    modes = ("on", "off") if (repeat + arms.index(arm)) % 2 else ("off", "on")
                    plan.extend(dict(wave, observer=mode) for mode in modes)
                else:
                    plan.append(wave)
    return plan


def run_id(wave, slot):
    suffix = "-" + wave["observer"] if "observer" in wave else ""
    if wave.get("oracle"):
        suffix = "-oracle"
    return f"r{wave['repeat']:03}-c{wave['width']:02}-{wave['arm']}-p{slot:02}{suffix}"



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
                    job["drmDevices"].update(observed["byDeviceAndRegion"])
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



@functools.lru_cache(maxsize=8)
def image_build_id(path, stamp):
    # Run before the timed barrier, caching only an unchanged file identity.
    try:
        note = subprocess.run(["readelf", "--notes", "--wide", str(path)], timeout=5,
                              check=True, capture_output=True, text=True,
                              env={**os.environ, "LC_ALL": "C"})
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError("unable to read pinned library ELF build ID") from error
    ids = re.findall(r"Build ID: ([0-9a-f]{8,128})\b", note.stdout)
    if len(ids) != 1 or file_stamp(path.stat()) != stamp:
        raise ValueError("ambiguous or changing ELF build ID")
    return ids[0]


def mapped_doe(progress, proc=Path("/proc")):
    """Observe the pinned library's actual executable mapping in the CDP-selected GPU child."""
    gpu, browser = progress["gpuProcessPid"], progress["browserPid"]
    if type(gpu) is not int or type(browser) is not int or min(gpu, browser) < 2 or gpu == browser:
        raise ValueError("owned browser and GPU child required")
    root = proc / str(gpu)
    if not chromium_gpu_process(root):
        raise ValueError("GPU child type unavailable")
    parent = gpu
    seen = set()
    while parent != browser:
        if parent <= 1 or parent in seen:
            raise ValueError("GPU process belongs to another browser")
        seen.add(parent)
        status = (proc / str(parent) / "status").read_text().splitlines()
        parent = int(next((line.partition(":")[2] for line in status if line.startswith("PPid:")), "0"))
    selected = progress["selection"]
    proof = read(Path(__file__).with_name("resident-command-witness-pin.json"))
    pin = selected["runtime"]["library"]
    compiler = [dep for dep in selected["dependencies"] if dep.get("sha256") == proof["compilerSha256"]]
    if selected["runtime"]["mode"] != "doe-c" or pin["sha256"] != proof["doeSha256"] or len(compiler) != 1:
        raise ValueError("archived witness runtime and compiler pins required")
    compiler_path = Path(compiler[0]["path"])
    if not compiler_path.is_absolute() or image_digest(compiler_path, file_stamp(compiler_path.stat())) != proof["compilerSha256"]:
        raise ValueError("pinned witness compiler bytes differ")
    path = Path(pin["path"])
    if not path.is_absolute() or not re.fullmatch("[0-9a-f]{64}", pin["sha256"]):
        raise ValueError("invalid pinned Doe library")
    stamp = file_stamp(path.stat())
    def mapped():
        for row in (root / "maps").read_text().splitlines():
            columns = row.split(None, 5)
            if len(columns) < 6 or "x" not in columns[1]:
                continue
            pathname = re.sub(r"\\([0-7]{3})", lambda m: chr(int(m.group(1), 8)), columns[5])
            major, minor = (int(number, 16) for number in columns[3].split(":"))
            if (os.makedev(major, minor), int(columns[4])) == stamp[:2] and pathname == str(path):
                return True
        return False
    if not mapped() or image_digest(path, stamp) != pin["sha256"] or file_stamp(path.stat()) != stamp or not mapped():
        raise ValueError("pinned Doe file and executable GPU mapping differ")
    build_id = image_build_id(path, stamp)
    if build_id != proof["doeBuildId"]:
        raise ValueError("pinned witness ELF build ID differs")
    return {"gpuPid": gpu, "sha256": pin["sha256"], "buildId": build_id,
            "compilerSha256": proof["compilerSha256"],
            "executableMappingVerified": True}


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


def capture_selection(wave, compositor_digest):
    arm = wave["arm"]
    if arm not in (SPARSE_ARMS if wave.get("sparseControls") else ARMS):
        raise ValueError("unknown resident comparison arm")
    if wave.get("oracle") and (not wave.get("sparseControls") or not arm.startswith("fawn") or "observer" in wave):
        raise ValueError("same-frame oracle requires a separate Fawn control")
    env = {"BLITZEN_TEST_COMPOSITOR_DIGEST": "cpu" if arm in CPU_ARMS else compositor_digest,
           "BLITZEN_REVIEW_COMPOSITOR_TRANSPORT": {PLANE_ARM:"stock-i420", "stock-helper":"native-helper"}.get(arm, "external-texture")}
    if wave.get("sparseControls"):
        env["BLITZEN_TEST_MATCHED_SPARSE"] = "1"
        if arm.startswith("fawn"):
            env["BLITZEN_TEST_FAWN_PLANE_EVIDENCE"] = "1"
        if wave.get("oracle"):
            env["BLITZEN_TEST_LINKED_SPARSE_ORACLE"] = "1"
    return env


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
                and k not in ("BLITZEN_RESIDENT_FRAME_TRACE_PREFIX", "BLITZEN_RESIDENT_STOCK_TRACE_PREFIX",
                              "BLITZEN_RESIDENT_OBSERVER")
            }
            mode = wave.get("observer")
            if mode:
                env["BLITZEN_RESIDENT_OBSERVER"] = mode
            if wave["arm"].startswith("fawn"):
                if getattr(args, "frame_import_evidence", False) or mode == "on":
                    env["BLITZEN_RESIDENT_FRAME_TRACE_PREFIX"] = str(row / "frame-import")
                env["BLITZEN_TEST_BROWSER_CONFIG"] = str(
                    args.doe if wave["arm"] == "fawn-doe" else args.dawn
                )
            else:
                env["BLITZEN_TEST_BROWSER_CHANNEL"] = args.stock_channel
                if getattr(args, "stock_executable", None):
                    env["BLITZEN_TEST_STOCK_EXECUTABLE"] = str(args.stock_executable)
                if mode == "on":
                    env["BLITZEN_RESIDENT_STOCK_TRACE_PREFIX"] = str(row / "stock-observer")
            if sources or sys.platform.startswith("linux"):
                env["BLITZEN_RESIDENT_OBSERVE_BROWSER_PID"] = "1"
            env.update(
                **capture_selection(wave, getattr(args, "compositor_digest", "cpu")),
                BLITZEN_TEST_ELEMENT_CAPTURE="1",
                BLITZEN_RESIDENT_PHYSICAL="1",
                BLITZEN_RESIDENT_RECEIPT_FILE=str(row / "outcome.json"),
                BLITZEN_RESIDENT_PROGRESS_FILE=str(row / "progress.json"),
                BLITZEN_RESIDENT_BARRIER_FILE=str(gate),
            )
            with (row / "fixture.log").open("wb") as log:
                child = subprocess.Popen(
                    ["node", str(FIXTURE)], env=env, stdout=log, stderr=subprocess.STDOUT
                )
            jobs.append({"dir": row, "child": child, "peak": 0, "samples": 0, "drmDevices": set(),
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
                            if mode or wave.get("oracle"):
                                boot = Path("/proc/sys/kernel/random/boot_id").read_bytes().strip()
                                if not re.fullmatch(rb"[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}", boot):
                                    raise ValueError("missing physical host boot identity")
                                observed["hostBootSha256"] = hashlib.sha256(boot).hexdigest()
                            if wave["arm"] == "fawn-doe" and (mode == "on" or getattr(args, "frame_import_evidence", False)):
                                try:
                                    observed["doeMapping"] = mapped_doe(progress)
                                except (OSError, TypeError, KeyError, ValueError) as error:
                                    observed["doeMapping"] = {"executableMappingVerified": False, "reason": str(error)}
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
                                      "devicesSeen": sorted(job["drmDevices"]),
                                      "attempts": job["drmAttempts"]},
                },
            )


@functools.lru_cache(maxsize=1)
def frame_import_checker():
    return runpy.run_path(str(Path(__file__).with_name("check-wire-submit.py")))


def observer_evidence(path, wave, progress, outcome, source):
    """Validate the collector and its process before counting an instrumented workflow."""
    mode, arm = wave["observer"], wave["arm"]
    pid = progress.get("browserPid")
    if type(pid) is not int or pid < 2 or not source or not progress.get("targetId") or not progress.get("surfaceId"):
        raise ValueError("missing process-bound browser, target or capture")
    if (not re.fullmatch(r"[0-9a-f]{64}", source.get("chromeSha256", "")) or
            not all(git_revision(source.get(key)) for key in ("chromium", "dawn"))):
        raise ValueError("missing pinned browser sources")
    selection = progress["selection"]
    if selection != outcome.get("selection") or outcome.get("clock") != "request-repaint-through-authoritative-reconciliation":
        raise ValueError("selected browser or workflow clock differs")
    if arm.startswith("fawn"):
        forced = "doe-c" if arm == "fawn-doe" else "dawn"
        if selection["runtime"]["mode"] != forced or selection["executable"]["sha256"] != source["chromeSha256"]:
            raise ValueError("selected runtime or browser image differs")
    elif selection.get("channel") not in ("chrome", "chromium") or "runtime" in selection:
        raise ValueError("stock browser required")
    gpu_pid = progress.get("gpuProcessPid")
    selectors = ["--use-webgpu-runtime=" + arm.split("-")[-1]] if arm.startswith("fawn") else []
    if (type(gpu_pid) is not int or gpu_pid < 2 or gpu_pid == pid or progress.get("observedRuntime") !=
            dict(gpuProcessPid=gpu_pid, sandboxed=True, selectors=selectors,
                 frameObserver=mode == "on" and arm.startswith("fawn"))):
        raise ValueError("observed sandbox, GPU process or runtime flags differ")
    expected = dict(mode=mode, browserPid=pid, targetId=progress["targetId"], captureId=progress["surfaceId"],
                    category="disabled-by-default-gpu.dawn" if mode == "on" else None)
    if outcome.get("observer") != expected or progress.get("phase") != "reconciled":
        raise ValueError("collector belongs to a different workflow")
    if progress.get("softwareSandbox") or outcome.get("nativeScopeRequest") or outcome.get("mainWorldControl"):
        raise ValueError("software or diagnostic-only observers cannot enter the physical comparison")
    if mode == "off":
        if (outcome.get("diagnosticOnly") or (path / "stock-observer.complete.json").exists()
                or (path / "frame-import.complete.json").exists()):
            raise ValueError("trace activity in the observer-off control")
        return {}
    if mode != "on":
        raise ValueError("unexpected observer mode")
    if arm.startswith("stock"):
        if (outcome.get("diagnosticOnly") or read(path / "stock-observer.scope.json") != expected
                or read(path / "stock-observer.complete.json").get("dataLossOccurred") is not False
                or not isinstance(read(path / "stock-observer.trace.json").get("traceEvents"), list)):
            raise ValueError("missing complete stock-browser control trace")
        return {}
    checker = frame_import_checker()
    if arm == "fawn-doe":
        return {"commandTerminal": checker["check"](path, True)}
    return {"maskReadback": checker["MASK"]["check"](path)}


def canonical_pixel_evidence(progress, outcome):
    evidence = outcome.get("pixelAuthority")
    if not isinstance(evidence, dict) or evidence.get("schema") != "blitzen.canonical-capture/v1" or (
        evidence.get("representation") != "video-frame-copy-to-srgb-rgba8"
    ):
        raise ValueError("missing canonical restricted-frame evidence")
    stale, fresh = evidence["stale"], evidence["fresh"]
    measured = ("frameHostCopyBytes", "canonicalHashedBytes", "canonicalRehashedTiles")
    for name, pair in (("stale", stale), ("fresh", fresh)):
        if not isinstance(pair, dict):
            raise ValueError(name + " evidence is not a frame pair")
        before, checked = pair["reviewed"], pair["checked"]
        if not isinstance(before, dict) or not isinstance(checked, dict):
            raise ValueError(name + " canonical frame is missing")
        binding = ("sessionId", "grantId", "grantRevision", "width", "height", "tileSize", "sourcePixelFormat")
        if any(before.get(k) is None or before[k] != checked.get(k) for k in binding):
            raise ValueError(name + " frame pair changed grant, representation or geometry")
        if not all(isinstance(before[k], str) and before[k] for k in ("sessionId", "grantId", "sourcePixelFormat")):
            raise ValueError("missing restricted frame binding")
        if any(type(before[k]) is not int or before[k] <= 0 for k in ("width", "height", "tileSize", "grantRevision")):
            raise ValueError("invalid canonical tile geometry")
        width, height, tile = (before[k] for k in ("width", "height", "tileSize"))
        tiles = ((width + tile - 1) // tile) * ((height + tile - 1) // tile)
        if (type(before["mediaTimestampUs"]) is not int or type(checked["mediaTimestampUs"]) is not int
                or not 0 <= before["mediaTimestampUs"] < checked["mediaTimestampUs"]):
            raise ValueError("frame verification lacks a newer capture from the same grant")
        for frame in (before, checked):
            hashes = frame.get("tileHashes")
            if (not isinstance(hashes, list) or not 0 < len(hashes) == tiles <= 256
                    or not all(isinstance(h, str) and re.fullmatch(r"[0-9a-f]{64}", h) for h in hashes)):
                raise ValueError("incomplete canonical tile digests")
            if any(type(frame.get(k)) is not int or frame[k] < 0 for k in measured):
                raise ValueError("incomplete canonical copy or hash accounting")
            if type(frame.get("externalHintMissedChange")) is not bool:
                raise ValueError("unknown external hint outcome")
        differences = [i for i, (a, b) in enumerate(zip(before["tileHashes"], checked["tileHashes"])) if a != b]
        if name == "stale":
            changed = stale.get("changedTiles")
            if (not differences or not isinstance(changed, list) or any(type(i) is not int for i in changed)
                    or changed != differences or stale.get("refusal") != "stale-observation"
                    or type(stale.get("effectsBefore")) is not int or stale["effectsBefore"] != 0
                    or type(stale.get("effectsAfter")) is not int or stale["effectsAfter"] != stale["effectsBefore"]):
                raise ValueError("changed canonical evidence did not refuse the stale action without an effect")
        else:
            if differences or before["sessionId"] != progress.get("surfaceId"):
                raise ValueError("fresh write lacks matching pixels on the measured capture")
            if (type(fresh.get("effectCount")) is not int or fresh["effectCount"] != outcome["knownEffects"]
                    or fresh.get("reconciliation") != "effect-present"):
                raise ValueError("fresh write did not reconcile with the measured application effect")
            for key in measured:
                if type(outcome.get(key)) is not int or outcome[key] < before[key] + checked[key]:
                    raise ValueError("timed workflow omits canonical evidence cost: " + key)
            misses = outcome.get("externalHintMissedChanges")
            if type(misses) is not int or misses < sum(f["externalHintMissedChange"] for f in (before, checked)):
                raise ValueError("timed workflow omits external hint misses")
    return {"representation": evidence["representation"], "staleTiles": len(stale["changedTiles"]),
            "freshEffectCount": fresh["effectCount"], "sessionId": fresh["checked"]["sessionId"]}


def capture_control(wave, progress, outcome):
    arm = wave["arm"]
    required_transport = capture_selection(wave, "cpu")["BLITZEN_REVIEW_COMPOSITOR_TRANSPORT"]
    if outcome.get("compositorTransport") != required_transport:
        raise ValueError("selected capture transport differs")
    refresh = outcome["grantRefresh"]
    fresh = outcome["pixelAuthority"]["fresh"]
    before, checked = fresh["reviewed"], fresh["checked"]
    if (refresh.get("staleSessionRefused") is not True or not refresh.get("priorSession")
            or refresh["priorSession"] == refresh["newSession"] or refresh["newSession"] != progress["surfaceId"]
            or refresh["newSession"] != before["sessionId"] or refresh["grantId"] != before["grantId"]
            or not 0 < refresh["priorRevision"] < refresh["newRevision"] == before["grantRevision"]
            or refresh["permissionRequests"] < 1 or refresh["newFrameRgbaBytes"] != before["width"] * before["height"] * 4
            or refresh["newFrameHostCopyBytes"] < refresh["newFrameRgbaBytes"]):
        raise ValueError("replacement grant or full canonical refresh is unproven")
    detail = {"transport": required_transport, "grantRefresh": refresh}
    if arm == PLANE_ARM:
        if (outcome.get("digestMode") != "cpu" or not before.get("stockPlaneSource") or not before.get("stockPlaneSparseTiles")
                or not checked.get("stockPlaneEqualityReused") or outcome["stockPlaneExportBytes"] <= 0
                or outcome["stockCanonicalRgbaCopyBytes"] <= 0 or outcome["stockPlaneSparseFrames"] <= 0
                or outcome["stockPlaneEqualFrames"] <= 0
                or outcome["frameHostCopyBytes"] != outcome["stockPlaneExportBytes"] + outcome["stockCanonicalRgbaCopyBytes"]):
            raise ValueError("stock JS plane mask fell back or capture accounting differs")
        source_bytes = outcome["stockSourceImageReadBytesBeforeCapture"]
        if type(source_bytes) is not int or source_bytes < 0:
            raise ValueError("missing pre-capture stock image verification bytes")
        detail["sourceImageReadBytesBeforeCapture"] = source_bytes
    if arm.startswith("fawn") and (not before.get("nativePlaneSparseTilesReused")
            or not checked.get("nativePlaneEqualityReused") or outcome["nativePlaneSnapshotSamples"] <= 0
            or type(outcome["nativePlaneSnapshotCopyBytes"]) is not int or outcome["nativePlaneSnapshotCopyBytes"] < 0):
        raise ValueError("linked native sparse/equality path or plane accounting is missing")
    return detail


def oracle_report(path, wave):
    """An independently observed full-frame read is a correctness expense outside timed cohorts."""
    data = {"verified": False, "latencyQualified": False}
    try:
        progress, outcome, ex, source = (read(path / (name + ".json")) for name in ("progress", "outcome", "exit", "browser-source"))
        partial = progress.get("partial") or {}
        for key in ("plannerCalls", "modelSpendUsd"):
            data[key] = outcome.get(key, partial.get(key))
        if not ex.get("allReady") or ex.get("exit") != 0:
            raise ValueError(progress.get("failure") or "no completed oracle")
        select = outcome["selection"]
        expected = "doe-c" if wave["arm"] == "fawn-doe" else "dawn"
        if (select != progress["selection"] or select["runtime"]["mode"] != expected or not outcome.get("diagnosticOnly")
                or not outcome.get("verified") or not outcome.get("imageCorrect") or outcome.get("knownEffects") != 1
                or outcome.get("committedRevision") != "B-r2" or not hardware_adapter(progress.get("gpu"))
                or select["executable"]["sha256"] != source["chromeSha256"]
                or not re.fullmatch(r"[0-9a-f]{64}", source["hostBootSha256"])):
            raise ValueError("oracle lacks selected physical image, verified write or host")
        runtime = progress["observedRuntime"]
        selector = "--use-webgpu-runtime=" + wave["arm"].split("-")[-1]
        if (runtime.get("gpuProcessPid") != progress["gpuProcessPid"] or runtime.get("sandboxed") is not True
                or runtime.get("selectors") != [selector] or runtime.get("frameObserver") is not False):
            raise ValueError("oracle browser selector or sandbox differs")
        canonical_pixel_evidence(progress, outcome)
        control = capture_control(wave, progress, outcome)
        proof = outcome["linkedSparseOracle"]
        if (proof.get("schema") != "blitzen.same-frame-sparse-correctness/v1" or proof.get("sameFrameOracle") is not True
                or proof.get("maskSource") != "linked-restricted-reader" or proof.get("diagnosticOnly") is not True
                or proof["verifiedFrames"] < 2 or proof["oracleCopyBytes"] <= 0 or proof["oracleHashBytes"] != proof["oracleCopyBytes"]):
            raise ValueError("complete same-frame oracle is missing")
        for key in ("reviewed", "checked"):
            observed = proof[key]
            frame = outcome["pixelAuthority"]["fresh"][key]
            for k in ("sessionId", "grantId", "grantRevision", "mediaTimestampUs"):
                if frame[k] != observed[k]:
                    raise ValueError("oracle frame is from another grant or timestamp")
            if (frame["tileHashes"] != observed["hashes"] or observed["producerCopyBytes"] != frame["frameHostCopyBytes"]
                    or observed["producerHashBytes"] != frame["canonicalHashedBytes"]
                    or observed["oracleCopyBytes"] != frame["width"] * frame["height"] * 4):
                raise ValueError("same-frame pixels or copy attribution differs")
        before, after = proof["reviewed"], proof["checked"]
        if (not before["usedNativeTiles"] or not before["changed"] or before["rejectedTileReads"] != 0
                or len(before["candidates"]) >= len(before["hashes"]) or not set(before["changed"]).issubset(before["candidates"])
                or not 0 < before["producerCopyBytes"] < before["oracleCopyBytes"] or after["changed"] != []
                or not after["usedNativeEquality"] or after["producerCopyBytes"] != 0):
            raise ValueError("native comparator omitted or failed to narrow canonical work")
        data.update(verified=True, canonicalControl=control, hostBootSha256=source["hostBootSha256"],
                    chromeSha256=source["chromeSha256"], selection=select, verifiedFrames=proof["verifiedFrames"],
                    oracleCopyBytes=proof["oracleCopyBytes"], oracleHashBytes=proof["oracleHashBytes"],
                    plannerCalls=outcome["plannerCalls"], modelSpendUsd=outcome["modelSpendUsd"])
    except (OSError, KeyError, TypeError, ValueError) as error:
        data["reason"] = str(error)
    return data



def summarize_row(path, wave, slot):
    p, e, o = (read(path / file) for file in ("progress.json", "exit.json", "outcome.json"))
    partial = p.get("partial") or {}
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
        "compositorTransport": o.get("compositorTransport"),
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
        "usageReceipts": {
            k: "outcome" if k in o else "partial" if k in partial else None
            for k in ("plannerCalls", "modelSpendUsd")
        },
        **{k: o.get(k, partial.get(k)) for k in COUNTERS},
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
    elif o.get("diagnosticOnly") is True and "observer" not in wave:
        if o.get("diagnosticKind") == "frame-import":
            try:
                checker = frame_import_checker()
                importer = checker["INPUT"]
                events, scope, browser = importer["read_row"](path)
                importer["trace_input"](events, scope, browser)
                data["frameImport"] = dict(restrictedFrameImportVerified=True,
                                           nativeDispatchVerified=False, latencyQualified=False)
                reason = "restricted frame import verified; native GPU completion unproven"
                if "frameImportDimensions" in o:
                    try:
                        data["maskReadback"], command = checker["MASK"]["traced_mask"](events, scope, browser)
                        reason = "tile diff submitted and mask mapped; native backend completion unproven"
                        if wave["arm"] == "fawn-doe":
                            try:
                                data["wireSubmit"] = checker["wire_submit"](
                                    events, scope, browser, p.get("gpuProcessPid"), command)
                                reason = "GPU-process mask submit identified; backend acceptance and completion unproven"
                                try:
                                    source = data["browserSource"] or {}
                                    data["commandTerminal"] = checker["command_terminal"](
                                        events, data["wireSubmit"], source.get("doeMapping") or {},
                                        p["selection"]["runtime"]["library"]["sha256"])
                                    reason = "selected Doe command accepted and completed; traced diagnostic"
                                except (KeyError, TypeError, ValueError) as error:
                                    data["commandTerminal"] = dict(nativeBackendCompletionVerified=False, latencyQualified=False)
                                    reason = f"GPU-process mask submit identified; command completion unproven: {error}"
                            except (KeyError, TypeError, ValueError) as error:
                                data["wireSubmit"] = dict(nativeQueueSubmitInvoked=False, latencyQualified=False)
                                reason = f"tile diff submitted and mask mapped; natural wire submit unproven: {error}"
                    except (KeyError, TypeError, ValueError) as error:
                        data["maskReadback"] = dict(submittedTileDiffVerified=False, latencyQualified=False)
                        reason = f"restricted frame import verified; mask result unproven: {error}"
            except (OSError, TypeError, KeyError, ValueError) as error:
                data["frameImport"] = {"restrictedFrameImportVerified": False, "latencyQualified": False}
                reason = f"restricted frame import unproven: {error}"
        else:
            reason = "capture-authority diagnostic adds track observations and a fence wait"
        data.update(status="unqualified", reason=reason)
    elif wave["arm"] == "fawn-doe" and wave.get("observer") != "on":
        # A native producer must bind this target's active capture and diff command
        # to completed work on the loaded Doe device. Flags, adapter strings or a
        # neighboring page's marker cannot discharge this gate.
        data.update(
            status="unattributed",
            reason="independent native page dispatch and completion proof unavailable",
        )
    elif wave["arm"] not in CPU_ARMS and not hardware_adapter(data.get("gpu")):
        data.update(status="unqualified", reason="working hardware adapter unconfirmed")
    else:
        try:
            if "observer" in wave:
                data.update(observer_evidence(path, wave, p, o, data["browserSource"]))
            data["pixelAuthority"] = canonical_pixel_evidence(p, o)
            if wave.get("sparseControls"):
                data["captureControl"] = capture_control(wave, p, o)
        except (OSError, TypeError, ValueError, KeyError) as error:
            data.update(status="unqualified", reason=str(error))
            for field in ("requestMonotonicMs", "mutationReturnedMonotonicMs",
                          "outcomeVerifiedMonotonicMs", "browserMutationEpochMs"):
                data[field] = None
            return data
        start, changed, finish = (
            data[k]
            for k in ("requestMonotonicMs", "mutationReturnedMonotonicMs", "outcomeVerifiedMonotonicMs")
        )
        if (
            not all(type(n) in (float, int) and math.isfinite(n) for n in (start, changed, finish))
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
    if wave["arm"] == "fawn-doe" and data["status"] != "verified":
        # Withhold timings when backend completion on the actual page is unknown.
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


def bounded_stats(intervals):
    midpoints = stats([(low + high) / 2 for low, high in intervals])
    lower = stats([low for low, _ in intervals])
    upper = stats([high for _, high in intervals])
    return {
        **midpoints,
        "p50BoundsMs": [lower["p50Ms"], upper["p50Ms"]] if intervals else None,
        "p95BoundsMs": [lower["p95Ms"], upper["p95Ms"]] if intervals else None,
    }


def usage_totals(rows, field):
    values = []
    complete = 0
    for row in rows:
        value = row[field]
        if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
            continue
        if field == "plannerCalls" and type(value) is not int:
            continue
        values.append(value)
        if row["usageReceipts"][field] == "outcome":
            complete += 1
    total_fn = sum if field == "plannerCalls" else math.fsum
    observed = total_fn(values) if values else None
    return {"observedSum": observed, "observedAttempts": len(values), "finalAttempts": complete,
            "total": observed if complete == len(rows) else None}


def capture_byte_summary(samples):
    measurements = {}
    for field in COUNTERS:
        if field.endswith("Bytes"):
            values = [r[field] for r in samples if type(r[field]) is int and r[field] >= 0]
            measurements[field] = {"observations": len(values), "median": statistics.median(values) if values else None}
    return measurements


def physical_pair_mismatch(left, right):
    a, b = left["browserSource"], right["browserSource"]
    identity = a.get("hostBootSha256", "")
    if not re.fullmatch(r"[0-9a-f]{64}", identity) or identity != b.get("hostBootSha256"):
        return "host boot differs or is unobserved"
    if left["arm"].startswith("fawn") and right["arm"].startswith("fawn") and a["chromeSha256"] != b["chromeSha256"]:
        return "Fawn executable differs"
    if any(row["arm"] in CPU_ARMS for row in (left, right)):
        return None  # The native CPU helper has no WebGPU device requirement.
    if left["compositorDigest"] != right["compositorDigest"]:
        return "digest mode differs"
    devices = (left["gpuProcessDrm"] or {}).get("devicesSeen", [])
    if (len(devices) != 1 or not re.fullmatch(r"[\w-]+@[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]", devices[0])
            or devices != (right["gpuProcessDrm"] or {}).get("devicesSeen")):
        return "a single matching kernel DRM device was not observed"
    return None


def summarize_plan(out, plan, oracles=None):
    arms = SPARSE_ARMS if any(w.get("sparseControls") for w in plan) else ARMS
    pairs_to_check = (*PAIRS, (PLANE_ARM, "stock-dawn"), (PLANE_ARM, "fawn-dawn")) if PLANE_ARM in arms else PAIRS
    rows = [summarize_row(out / run_id(w, s), w, s) for w in plan for s in range(1, w["width"] + 1)]
    for row in rows:
        if row.get("sparseControls") and row["arm"].startswith("fawn") and row["status"] == "verified":
            control = (oracles or {}).get(row["arm"], {})
            source = row["browserSource"] or {}
            if (not control.get("verified") or control["selection"] != row["selection"]
                    or control["chromeSha256"] != source.get("chromeSha256")
                    or control["hostBootSha256"] != source.get("hostBootSha256")):
                row.update(status="unqualified", reason="same-frame correctness on this selected image and host is missing",
                           pageChangeToVerifiedBoundsMs=None, pageChangeToVerifiedMidpointMs=None,
                           requestMonotonicMs=None, mutationReturnedMonotonicMs=None, outcomeVerifiedMonotonicMs=None)
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
        for arm in arms:
            items = [r for r in rows if r["width"] == width and r["arm"] == arm]
            samples = [r for r in items if r["status"] == "verified" and r["fullMeasuredOverlap"]]
            by_arm.append(
                {
                    "arm": arm,
                    "concurrency": width,
                    "planned": len(items),
                    "statuses": dict(collections.Counter(r["status"] for r in items)),
                    "plannerCalls": usage_totals(items, "plannerCalls"),
                    "modelSpendUsd": usage_totals(items, "modelSpendUsd"),
                    **({"verifiedCaptureBytes": capture_byte_summary(samples)} if PLANE_ARM in arms else {}),
                    "excludedIncompleteWaves": sum(
                        r["status"] == "verified" and not r["fullMeasuredOverlap"] for r in items
                    ),
                    **bounded_stats([r["pageChangeToVerifiedBoundsMs"] for r in samples]),
                }
            )
        for left, right in pairs_to_check:
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
                    if "observer" in a:
                        reason = physical_pair_mismatch(a, b)
                        if reason:
                            skipped[reason] += 1
                            continue
                    baseline_min, baseline_max = a["pageChangeToVerifiedBoundsMs"]
                    candidate_min, candidate_max = b["pageChangeToVerifiedBoundsMs"]
                    deltas.append((candidate_min - baseline_max, candidate_max - baseline_min))
            pairs.append(
                {
                    "baseline": left,
                    "candidate": right,
                    "concurrency": width,
                    "omitted": dict(skipped),
                    **bounded_stats(deltas),
                }
            )
    return {
        "clock": "Host monotonic bounds bracket the page canvas mutation, ending at authoritative application reconciliation. Midpoint is an estimate. No compositor frame-commit stamp.",
        "usageScope": "Per-arm usage includes all planned attempts, including refusals and incorrect outcomes. The observed sum includes partial receipts. A total requires a final measured value for every attempt.",
        "aggregation": "Nearest-rank p95 and median p50 of the qualified observations. Paired deltas subtract the baseline from the candidate in the same repeat and slot. Bounds carry the observed mutation-time uncertainty through each quantile. A delta interval containing zero leaves the direction unresolved. Sampling uncertainty is separate. Every planned attempt is reported and incomplete overlapping cohorts are excluded.",
        "memoryScope": "Process-tree RSS is sampled about every 250 ms. From browser readiness, Linux DRM counts resident bytes reported for the fixture GPU process. The peak contains one sample across regions, deduplicated by device and DRM client. Separate clients can count shared buffers twice. Missing samples are unknown. Whole-device VRAM is unmeasured.",
        "attribution": "Matched source revisions and versions permit a whole-browser comparison. Fawn/Doe observations enter timing only in the matched trace-on cohort after the same page command completes on the pinned native runtime. Its terminal identifies the GPU command that ran. The pixel-authority receipt separately identifies the canonical evidence used to approve the write.",
        "byArm": by_arm,
        "paired": pairs,
        "rows": rows,
    }


def summarize(out):
    plan = read(out / "plan.json")
    if not isinstance(plan, list) or not plan:
        raise ValueError("missing frozen comparison plan")
    controls = [wave for wave in plan if wave.get("oracle")]
    plan = [wave for wave in plan if not wave.get("oracle")]
    if any(w.get("sparseControls") for w in plan):
        if not all(w.get("sparseControls") and "observer" in w for w in plan) or sorted(w["arm"] for w in controls) != ["fawn-dawn", "fawn-doe"]:
            raise ValueError("incomplete sparse comparison plan")
    oracles = {w["arm"]: oracle_report(out / run_id(w, 1), w) for w in controls}
    if not any("observer" in wave for wave in plan):
        return summarize_plan(out, plan)
    cohorts = {mode: [wave for wave in plan if wave.get("observer") == mode] for mode in ("off", "on")}
    def keys(waves):
        return [(wave["arm"], wave["repeat"], wave["width"]) for wave in waves]
    off_keys, on_keys = (keys(cohorts[mode]) for mode in ("off", "on"))
    if (len(off_keys) + len(on_keys) != len(plan) or sorted(off_keys) != sorted(on_keys)
            or len(set(off_keys)) != len(off_keys)):
        raise ValueError("observer plan must pair each arm, repeat and concurrency exactly once")
    off, on = (summarize_plan(out, cohorts[mode], oracles) for mode in ("off", "on"))
    by_key = {(r["arm"], r["width"], r["repeat"], r["slot"]): r for r in off["rows"]}
    effect = []
    for width in sorted({w["width"] for w in plan}):
        for arm in (SPARSE_ARMS if controls else ARMS):
            bounds, omitted = [], collections.Counter()
            for traced in (r for r in on["rows"] if r["width"] == width and r["arm"] == arm):
                bare = by_key[arm, width, traced["repeat"], traced["slot"]]
                if not (traced["fullMeasuredOverlap"] and bare["fullMeasuredOverlap"]):
                    omitted[f"off={bare['status']}, on={traced['status']}"] += 1
                    continue
                if (not comparable_browsers(bare, traced)
                        or bare["browserSource"]["chromeSha256"] != traced["browserSource"]["chromeSha256"]
                        or bare["compositorDigest"] != traced["compositorDigest"] or bare["gpu"] != traced["gpu"]
                        or physical_pair_mismatch(bare, traced)):
                    omitted["image, GPU or digest mode differs"] += 1
                    continue
                low, high = traced["pageChangeToVerifiedBoundsMs"]
                base_low, base_high = bare["pageChangeToVerifiedBoundsMs"]
                bounds.append((low - base_high, high - base_low))
            effect.append(dict(arm=arm, concurrency=width, omitted=dict(omitted), **bounded_stats(bounds)))
    return {"scope": "Trace-on and trace-off measurements are separate. Observer-effect bounds subtract the same image and arm with tracing off. A missing backend witness leaves that comparison unresolved. No overhead is subtracted from cross-arm results.",
            "traceOn": on, "traceOff": off, "observerEffect": effect, **({"sparseCorrectness": oracles} if controls else {})}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="op", required=True)
    for command in ("plan", "run", "report"):
        p = sub.add_parser(command)
        p.add_argument("--out", type=Path, required=True)
        if command == "plan":
            p.add_argument("--sparse-controls", action="store_true", help="add stock JS I420, require native sparse paths and separate same-frame correctness on each Fawn profile")
            p.add_argument("--repeats", type=int, default=20)
            p.add_argument("--paired-observer", action="store_true",
                           help="interleave a common CDP trace and an unchanged control per arm and repeat")
            p.add_argument("--concurrency", type=int, nargs="+", default=[1, 2])
        if command == "run":
            p.add_argument("--dawn", type=Path, required=True)
            p.add_argument("--doe", type=Path, required=True)
            p.add_argument("--source-receipts", nargs=2, type=Path, metavar=("STOCK", "FAWN"),
                           help="bind both Linux browser images to their operator-verified source receipts")
            p.add_argument("--compositor-digest", choices=["cpu", "webgpu"], default="cpu",
                           help="digest for the three browser GPU arms; the native helper retains its own CPU digest")
            p.add_argument("--frame-import-evidence", action="store_true",
                           help="record the restricted VideoFrame import on both Fawn arms; traced rows are diagnostic")
            p.add_argument("--stock-executable", type=Path, help="select a sandboxed stock image verified against its source receipt")
            p.add_argument("--stock-channel", choices=["chrome", "chromium"], default="chrome")
            p.add_argument("--timeout", type=int, default=175)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.op == "plan":
        plan = make_plan(args.repeats, args.concurrency, args.paired_observer, args.sparse_controls)
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
        if args.stock_executable:
            if not args.sources:
                parser.error("an explicitly selected stock image requires process-bound source receipts")
            args.stock_executable = args.stock_executable.resolve(strict=True)
        if args.frame_import_evidence and not args.sources:
            parser.error("frame import evidence requires process-bound source receipts")
        plan = read(out / "plan.json")
        if not plan:
            parser.error("create the plan first")
        if any("observer" in wave for wave in plan) and (not args.sources or args.frame_import_evidence):
            parser.error("paired observers require both process-bound source receipts and select tracing from the plan")
        with (out / "run-started").open("x") as f:
            f.write(str(os.getpid()))
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
        for wave in plan:
            print(wave, flush=True)
            run_wave(out, wave, args)
            if wave.get("oracle"):
                control = oracle_report(out / run_id(wave, 1), wave)
                if not control["verified"]:
                    write(out / "report.json", summarize(out))
                    parser.error("physical sparse control refused: " + control["reason"])
        write(out / "report.json", summarize(out))
    else:
        write(out / "report.json", summarize(out))
        print(out / "report.json")


if __name__ == "__main__":
    main()
