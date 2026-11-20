"""Atomic, bounded windows for the existing matched physical comparison. Linux only."""

import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time
import uuid


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def stable(data):
    return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def atomic(path, value):
    tmp = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    with tmp.open("x") as handle:
        json.dump(value, handle, indent=2)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)
    fd = os.open(path.parent, os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def read(path):
    return json.loads(path.read_text())


def groups(plan, original):
    # Reproduce the exact sparse plan, including rotating arm and observer order.
    widths = list(dict.fromkeys(w["width"] for w in plan if not w.get("oracle")))
    count = max(w["repeat"] for w in plan)
    if plan != original.make_plan(count, widths, sparse_controls=True):
        raise ValueError("complete ordered sparse plan required")
    output = []
    for w in plan:
        key = (w["repeat"], w["width"])
        if not output or key != (output[-1][0]["repeat"], output[-1][0]["width"]):
            output.append([])
        output[-1].append(w)
    return output


def runtime_inputs(out, args, original):
    if sys.platform != "linux" or not args.sources or not args.stock_executable:
        raise ValueError(
            "physical block mode requires all source receipts and explicit stock executable on Linux"
        )
    device = args.expected_drm_device
    candidates = set()
    for render in Path("/sys/class/drm").glob("renderD*/device"):
        if (render / "driver").exists():
            candidates.add((render / "driver").resolve().name + "@" + render.resolve().name)
    if device not in candidates:
        raise ValueError("declared DRM device absent from render nodes")
    workspace = Path(original.__file__).resolve().parents[1]
    sourcefiles = [
        p
        for folder in ("src", "qualification", "test/support", "test/browser")
        for p in (workspace / folder).rglob("*")
        if p.suffix in (".js", ".mjs", ".cjs", ".py", ".cc") and "__pycache__" not in p.parts
    ]
    tree = workspace.parents[1]
    sourcefiles.extend((workspace / "qualification").glob("*.json"))
    sourcefiles.append(workspace / "package-lock.json")
    sourcefiles.extend(
        tree / name
        for name in (
            "browser/chromium/scripts/browser-runtime-selector.mjs",
            "config/fawn-workspace-service.json",
            "config/fawn-workspace-client.json",
            "packages/doe-gpu/package.json",
            "packages/doe-gpu/src/vendor/webgpu/platform-package.js",
        )
    )
    fawn = read(args.doe)["browser"]["fawnBuild"]
    fawn_exe = Path(fawn["executable"]["path"]).resolve(strict=True)
    images = {
        args.stock_executable: args.sources["stock"]["chromeSha256"],
        fawn_exe: args.sources["fawn"]["chromeSha256"],
    }
    for item in [fawn["runtime"]["library"], *fawn["dependencies"]]:
        images[Path(item["path"]).resolve(strict=True)] = item["sha256"]
    for name, expected in args.sources["fawn"].get("components", {}).items():
        if Path(name).name != name:
            raise ValueError("invalid Fawn component name")
        images[fawn_exe.parent / name] = expected
    for image, expected in images.items():
        if digest(image) != expected:
            raise ValueError("frozen image bytes differ: " + str(image))
    executable = Path(shutil.which("node") or "").resolve(strict=True)
    playwright = (workspace / "node_modules/playwright-core").resolve(strict=True)
    modules = {
        str(p.relative_to(playwright)): digest(p)
        for p in playwright.rglob("*")
        if p.is_file() and p.suffix in (".js", ".json", ".mjs", ".cjs")
    }
    env = {
        k: v
        for k, v in os.environ.items()
        if k.startswith(("BLITZEN_", "FAWN_", "DOE_", "VK_", "DRI_", "MESA_", "LIBGL_"))
        or k in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE", "LANG", "LD_LIBRARY_PATH", "LD_PRELOAD")
    }
    return {
        "schema": "blitzen.physical-matched-blocks/v1",
        "planSha256": digest(out / "plan.json"),
        "hostBootSha256": hashlib.sha256(
            Path("/proc/sys/kernel/random/boot_id").read_bytes().strip()
        ).hexdigest(),
        "expectedDrmDevice": device,
        "sourceReceipts": args.sources,
        "profileDigests": {"dawn": digest(args.dawn), "doe": digest(args.doe)},
        "stockExecutable": str(args.stock_executable),
        "stockChannel": args.stock_channel,
        "timeout": args.timeout,
        "compositorDigest": args.compositor_digest,
        "software": stable({str(p.relative_to(tree)): digest(p) for p in sourcefiles}),
        "verifiedImages": {str(p): value for p, value in images.items()},
        "preflightImageBytesPerWindow": sum(p.stat().st_size for p in images),
        "nodeExecutable": str(executable),
        "nodeSha256": digest(executable),
        "playwrightClosure": stable(modules),
        "captureEnvironmentDigest": stable(env),
        "planner": "canvas-review deterministic rule; all observed calls and costs retained",
    }


def row_names(waves, orig):
    return [(wave, slot, orig.run_id(wave, slot)) for wave in waves for slot in range(1, wave["width"] + 1)]


def raw_costs(path, waves, orig):
    rows = []
    for w, slot, name in row_names(waves, orig):
        row = path / name
        if not row.is_dir():
            continue

        def available(file):
            try:
                return orig.read(row / file)
            except (FileNotFoundError, json.JSONDecodeError):
                return {}

        progress = available("progress.json")
        partial = progress.get("partial") or {}
        outcome = available("outcome.json")
        terminal = available("exit.json")
        rows.append(
            {
                "row": name,
                "terminal": bool(terminal),
                "phase": progress.get("phase"),
                "failure": progress.get("failure"),
                "exit": terminal.get("exit"),
                "waveTimeout": terminal.get("waveTimeout"),
                **{key: outcome.get(key, partial.get(key)) for key in ("plannerCalls", "modelSpendUsd")},
            }
        )
    return rows


def publish(source, dest):
    os.rename(source, dest)
    for parent in (source.parent, dest.parent):
        fd = os.open(parent, os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def payload(path):
    files = {}
    for p in sorted(path.rglob("*")):
        if p.is_symlink():
            raise ValueError("unexpected symlink inside raw block")
        if p.is_file() and p != path / "block.json":
            files[str(p.relative_to(path))] = digest(p)
    return files


def check_seal(dest, waves, identity):
    seal = read(dest / "block.json")
    if seal["contract"] != stable(identity) or seal["waves"] != waves or payload(dest) != seal["files"]:
        raise ValueError("accepted block content changed: " + dest.name)


def report(out, orig):
    root = out / "block-ledger"
    with (root / ".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_SH)
        try:
            plan = read(out / "plan.json")
            identity = read(root / "identity.json")
            if identity["planSha256"] != digest(out / "plan.json"):
                raise ValueError("plan changed")
            for i, waves in enumerate(groups(plan, orig)):
                dest = root / "accepted" / f"{i:03}"
                if dest.exists():
                    check_seal(dest, waves, identity)
                for _, _, name in row_names(waves, orig):
                    link = out / name
                    if link.is_symlink():
                        if link.readlink() != dest.relative_to(out) / name:
                            raise ValueError("row from different block: " + name)
                    elif link.exists() or dest.exists():
                        raise ValueError("unbound visible or missing accepted row: " + name)
            result = orig.summarize(out)
            result["blockLedger"] = audit(out, plan, orig)
        except (KeyError, OSError, ValueError) as error:
            atomic(out / "report.json", {"timingQualified": False, "failure": str(error)})
            raise
        atomic(out / "report.json", result)


def audit(out, plan, orig):
    root = out / "block-ledger"
    if not root.exists():
        return None
    result = []
    for i, waves in enumerate(groups(plan, orig)):
        tag = f"{i:03}"
        for state in ("accepted", "incomplete", "pending"):
            folder = root / state / tag
            if folder.is_dir():
                result.append(
                    {
                        "block": i,
                        "state": state,
                        "repeat": waves[0]["repeat"],
                        "width": waves[0]["width"],
                        "plannedSlots": sum(w["width"] for w in waves),
                        **(
                            {"excludedAttempts": raw_costs(folder, waves, orig)}
                            if state != "accepted"
                            else {}
                        ),
                    }
                )
    return {
        "blocks": result,
        "partialTimingEligible": False,
        "costScope": "Excluded attempts retain raw files and separate per-row spend. Missing spend is unknown.",
    }


def bind_rows(attempt, waves, orig, identity, adapters):
    for wave, _, name in row_names(waves, orig):
        path = attempt / name
        ex = read(path / "exit.json")
        progress = orig.read(path / "progress.json")
        # A failed/timeout row is an observed outcome. Existing reporting rules retain it.
        if not ex.get("allReady") or ex.get("exit") != 0 or ex.get("waveTimeout"):
            continue
        src = read(path / "browser-source.json")
        expected = identity["sourceReceipts"]["fawn" if wave["arm"].startswith("fawn") else "stock"]
        if (
            src.get("hostBootSha256") != identity["hostBootSha256"]
            or src.get("chromeSha256") != expected["chromeSha256"]
            or any(src.get(k) != expected[k] for k in ("chromium", "dawn"))
        ):
            raise ValueError("observed browser/boot differs from frozen block contract: " + name)
        if wave["arm"] not in orig.CPU_ARMS:
            devices = (ex.get("gpuProcessDrm") or {}).get("devicesSeen")
            if devices != [identity["expectedDrmDevice"]]:
                raise ValueError("observed DRM adapter differs or could not be verified: " + name)
            info = progress.get("gpu")
            if not orig.hardware_adapter(info) or adapters.setdefault(wave["arm"], info) != info:
                raise ValueError("selected WebGPU adapter differs across reserved blocks: " + name)
        receipt = read(path / "outcome.json")
        if receipt.get("selection") != progress.get("selection"):
            raise ValueError("selection changed inside observed block: " + name)
        if receipt.get("plannerCalls") != 0 or receipt.get("modelSpendUsd") != 0:
            raise ValueError("fixed rule planner changed; raw calls/spend preserved: " + name)


def run(out, plan, args, orig, identity=None):
    schedule = groups(plan, orig)
    if not 1 <= args.matched_blocks <= len(schedule):
        raise ValueError("choose a positive number of whole matched blocks within the plan")
    root = out / "block-ledger"
    root.mkdir(exist_ok=True)
    with (root / ".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if identity is None:
            identity = runtime_inputs(out, args, orig)
        for name in ("accepted", "pending", "incomplete"):
            (root / name).mkdir(exist_ok=True)
        marker = out / "run-started"
        contract = root / "identity.json"
        if not contract.exists():
            if marker.exists() or any(out.glob("r[0-9]*")):
                raise ValueError("existing legacy or unbound run")
            atomic(contract, identity)
        elif read(contract) != identity:
            raise ValueError("frozen host, adapter, planner, inputs or plan changed")
        if not marker.exists():
            with marker.open("x") as f:
                f.write("physical-matched-blocks/v1\n")
        if marker.read_text() != "physical-matched-blocks/v1\n":
            raise ValueError("legacy run cannot join the block protocol")
        results = []
        adapters = {}
        for i, waves in enumerate(schedule):
            tag = f"{i:03}"
            pending = root / "pending" / tag
            dest = root / "accepted" / tag
            retired = root / "incomplete" / tag
            # Publish row paths in advance. All change from absent to visible in one directory rename.
            for _, _, name in row_names(waves, orig):
                link = out / name
                expected = Path("block-ledger") / "accepted" / tag / name
                if link.is_symlink():
                    if link.readlink() != expected:
                        raise ValueError("unexpected row location")
                elif link.exists():
                    raise ValueError("unbound existing row")
                else:
                    link.symlink_to(expected, target_is_directory=True)
            if dest.exists():
                check_seal(dest, waves, identity)
                expected = read(dest / "block.json")["adapters"]
                if any(adapters.setdefault(arm, info) != info for arm, info in expected.items()):
                    raise ValueError("inconsistent historical WebGPU identity")
                continue
            if retired.exists():
                if i == 0:
                    raise ValueError("interrupted correctness controls require a new study")
                continue
            if pending.exists():
                raise ValueError(
                    "partial block " + tag + " retained outside timing; no automatic retry; see block audit"
                )
            if len(results) >= args.matched_blocks or (root / "PAUSE").exists():
                break
            pending.mkdir()
            start = time.time_ns()
            try:
                for wave in waves:
                    orig.run_wave(pending, wave, args)
                    if wave.get("oracle"):
                        proof = orig.oracle_report(pending / orig.run_id(wave, 1), wave)
                        if proof.get("verified") is not True:
                            raise ValueError("physical sparse control refused: " + str(proof.get("reason")))
                bind_rows(pending, waves, orig, identity, adapters)
                atomic(
                    pending / "block.json",
                    {
                        "waves": waves,
                        "contract": stable(identity),
                        "startedEpochNs": start,
                        "completedEpochNs": time.time_ns(),
                        "adapters": adapters,
                        "files": payload(pending),
                    },
                )
                publish(pending, dest)
            except BaseException:
                # run_wave has waited for its children on a cooperative interrupt.
                # A hard-killed coordinator leaves pending, which fails closed on resume.
                if pending.exists():
                    publish(pending, retired)
                raise
            results.append(i)
        return results
