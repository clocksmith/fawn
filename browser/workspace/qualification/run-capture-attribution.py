#!/usr/bin/env python3
"""Linux correctness diagnostic for a fresh Blitzen/Fawn GPU capture. Never produces latency samples."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import signal
import subprocess
import sys
import time
import uuid

HERE = Path(__file__).resolve().parent
FIXTURE = HERE.parent / "test/browser/canvas-review.test.js"
OBSERVER = HERE / "trace-doe-vulkan-queue.py"
CORRELATOR = HERE / "correlate-document-submit.py"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text())


def write(path, value):
    with path.open("x") as f:
        json.dump(value, f, indent=2, allow_nan=False)
        f.write("\n")


def artifact(entry, deadline):
    path = Path(entry["path"])
    require(
        path.is_absolute() and path.is_file() and re.fullmatch("[0-9a-f]{64}", entry["sha256"]),
        "invalid pinned artifact",
    )
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            require(time.monotonic() < deadline, "deadline expired verifying the artifact")
            digest.update(block)
    require(digest.hexdigest() == entry["sha256"], "artifact digest differs: " + str(path))
    return {"path": str(path.resolve()), "sha256": digest.hexdigest()}


def gdb_base():
    exe = shutil.which("gdb")
    require(exe, "GDB required for the independent native observer")
    return [
        exe,
        "-nx",
        "-nh",
        "-q",
        "-batch",
        "-iex",
        "set auto-load off",
        "-iex",
        "set debuginfod enabled off",
    ]


def preflight(profile, scope_path, deadline):
    require(sys.platform == "linux", "live process verification requires Linux")
    browser = read(profile)["browser"]
    build = browser["fawnBuild"]
    require(
        browser["channel"] == "fawn"
        and build["runtime"]["mode"] == "doe-c"
        and set(build["runtime"]) == {"mode", "library"}
        and build["dependencies"],
        "expected the forced Doe profile with pinned dependency closure",
    )
    exe, deps = (
        artifact(build["executable"], deadline),
        [artifact(x, deadline) for x in build["dependencies"]],
    )
    lib = artifact(build["runtime"]["library"], deadline)
    candidates = [exe, *deps]
    scope = next(
        (
            x
            for x in candidates
            if x["path"] == str(scope_path.resolve() if scope_path else Path(exe["path"]))
        ),
        None,
    )
    require(scope, "scope image must be the executable or a pinned profile dependency")
    require(
        len({x["path"] for x in [*candidates, lib]}) == len(candidates) + 1,
        "pinned paths must be distinct",
    )
    for image, names in (
        (scope, ("FawnSubmitResidentDocumentCommandV1",)),
        (
            lib,
            (
                "wgpuQueueSubmit",
                "create_device",
                "doe_vk_commands_dispatch_resources",
                "doe_vk_commands_submit",
                "doe_vk_commands_release",
                "doe_vk_commands_poll",
                "doe_vk_commands_wait",
                "complete",
            ),
        ),
    ):
        # The observer reads real frame variables and the poll/wait owner of
        # complete(). A stripped symbol address alone cannot establish them.
        query = """import gdb
owner = gdb.objfiles()[0]
for name in NAMES:
    candidates = [*gdb.lookup_static_symbols(name), owner.lookup_global_symbol(name)]
    hits = {s.symtab.fullname() for s in candidates if s and s.symtab and s.symtab.objfile == owner}
    if name == "complete":
        hits = {path for path in hits if path.endswith("/backends/vulkan/src/commands.c")}
    if len(hits) == 1:
        print("validated " + name)
""".replace("NAMES", repr(names))
        remaining = deadline - time.monotonic()
        require(remaining > 0, "deadline expired before debug symbol checks")
        checked = subprocess.run(
            gdb_base() + ["--se", image["path"], "-ex", "python exec(" + repr(query) + ")"],
            capture_output=True,
            text=True,
            timeout=remaining,
        )
        missing = [n for n in names if "validated " + n not in checked.stdout.splitlines()]
        require(
            checked.returncode == 0 and not missing,
            "missing usable debug symbols in " + image["path"] + ": " + ", ".join(missing),
        )
    require(shutil.which("node"), "Node is required for the existing Blitzen fixture")
    return {
        "executable": exe,
        "scopeImage": scope,
        "doeLibrary": lib,
        "profile": str(profile.resolve()),
    }


def proc(pid):
    try:
        # comm may contain spaces or parentheses.
        words = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(words[1]), int(words[19]), words[0]
    except (FileNotFoundError, ProcessLookupError):
        return None


def belongs(pid, root):
    seen = set()
    while pid > 1 and pid not in seen:
        if pid == root:
            return True
        seen.add(pid)
        info = proc(pid)
        if not info:
            return False
        pid = info[0]
    return False


def sandbox_fields(pid):
    fields = {}
    for row in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, v = row.partition(":")
        if key in ("Seccomp", "Seccomp_filters", "NoNewPrivs"):
            fields[key] = int(v.strip())
    return fields


def check_ready(ready, node_pid, cfg):
    browser, gpu = ready["browserPid"], ready["gpuPid"]
    require(
        ready["run"] == cfg["run"]
        and ready["controllerPid"] == node_pid
        and type(browser) is int
        and type(gpu) is int
        and browser > 1
        and gpu > 1
        and len({node_pid, browser, gpu}) == 3,
        "controller or run identity differs",
    )
    require(
        belongs(browser, node_pid) and belongs(gpu, browser),
        "CDP PIDs are outside the fixture process tree",
    )
    exe = Path(cfg["executable"]["path"])
    require(
        all(os.path.samefile(f"/proc/{pid}/exe", exe) for pid in (browser, gpu)),
        "browser or GPU executable mapping differs",
    )
    args = Path(f"/proc/{gpu}/cmdline").read_bytes().split(b"\0")
    require(
        b"--type=gpu-process" in args
        and b"--fawn-resident-command-provenance" in args
        and b"--use-webgpu-runtime=doe" in args
        and os.fsencode("--doe-webgpu-library-path=" + cfg["doeLibrary"]["path"]) in args,
        "GPU process command line differs from the pinned diagnostic",
    )
    source, child = sandbox_fields(browser), sandbox_fields(gpu)
    require(
        ready.get("gpuSandboxed") is True
        and child.get("Seccomp") == 2
        and child.get("NoNewPrivs") == 1
        and child.get("Seccomp_filters", 0) > source.get("Seccomp_filters", 0),
        "GPU sandbox could not be verified independently",
    )
    kernel = proc(gpu)
    require(kernel and kernel[2] != "Z", "GPU no longer alive")
    return {
        "browserPid": browser,
        "gpuPid": gpu,
        "gpuStartTicks": kernel[1],
        "gpuSandbox": child,
    }


def observe_fixture():
    # This runs inside GDB. It is an ancestor of the new fixture, never a
    # debugger of an existing browser or a supplied PID.
    import gdb

    out = Path(os.environ["BLITZEN_CAPTURE_RUN_DIRECTORY"])
    cfg = read(out / "run.json")
    with (out / "fixture.log").open("x") as log:
        child = subprocess.Popen(
            [cfg["node"], str(FIXTURE)],
            cwd=str(HERE.parent),
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            while not (out / "ready.json").exists():
                if child.poll() is not None:
                    progress = read(out / "progress.json") if (out / "progress.json").exists() else {}
                    reason = str(progress.get("failure") or "see fixture.log")
                    if "No usable sandbox!" in reason:
                        reason = "Chromium reported no usable sandbox"
                    raise ValueError(
                        "fixture exited before GPU handoff, phase "
                        + progress.get("phase", "launch")
                        + ": "
                        + reason.partition("\n")[0][:500]
                    )
                require(
                    time.monotonic() < cfg["deadline"],
                    "fixture did not reach GPU handoff",
                )
                time.sleep(0.05)
            ready = read(out / "ready.json")
            identity = check_ready(ready, child.pid, cfg)
            gpu = identity["gpuPid"]
            gdb.execute("set confirm off")
            gdb.execute("set pagination off")
            gdb.execute(f"attach {gpu}")
            require(
                identity == check_ready(ready, child.pid, cfg) and gdb.selected_inferior().pid == gpu,
                "GPU process identity changed at handoff",
            )
            write(out / "owned.json", identity)
            runpy.run_path(str(OBSERVER))
            handoff = out / "armed.partial"
            write(handoff, {"run": cfg["run"], "gpuPid": gpu})
            handoff.rename(out / "armed.json")
            gdb.execute("continue")
            require(
                not gdb.selected_inferior().pid,
                "GPU stopped before a clean observer termination",
            )
            require(
                child.wait(timeout=max(1, cfg["deadline"] - time.monotonic())) == 0,
                "Resident Process fixture failed",
            )
            write(out / "fixture-exited.json", {"run": cfg["run"], "exitCode": 0})
        except Exception as error:
            write(out / "observer-error.json", {"reason": str(error)})
            raise
        finally:
            if child.poll() is None:
                child.terminate()


def descendants(root):
    # Linux can parent subprocesses to any thread. Walk only this fixture's
    # child lists, including descendants reparented to our own subreaper.
    stack = [root]
    seen = {root}
    while stack:
        owner = stack.pop()
        try:
            child_lists = list(Path(f"/proc/{owner}/task").glob("*/children"))
        except (FileNotFoundError, ProcessLookupError):
            continue
        for entry in child_lists:
            try:
                children = [int(text) for text in entry.read_text().split()]
            except (FileNotFoundError, ProcessLookupError):
                continue
            for pid in children:
                if pid not in seen:
                    seen.add(pid)
                    stack.append(pid)
                    yield pid


def cleanup_owned():
    # The CLI is a fresh subreaper. Playwright may create a separate process
    # group, so sweep only our own descendants and recheck birth time on signal.
    for sig, limit in ((signal.SIGTERM, 3), (signal.SIGKILL, 2)):
        until = time.monotonic() + limit
        while time.monotonic() < until:
            while True:
                try:
                    if os.waitpid(-1, os.WNOHANG)[0] == 0:
                        break
                except ChildProcessError:
                    break
            children = []
            for pid in descendants(os.getpid()):
                info = proc(pid)
                if info and info[2] != "Z":
                    children.append((pid, info[1]))
            if not children:
                return
            for pid, start in children:
                current = proc(pid)
                if current and current[1] == start and belongs(pid, os.getpid()):
                    try:
                        os.kill(pid, sig)
                    except ProcessLookupError:
                        pass
            time.sleep(0.1)
    raise ValueError("fixture descendants did not terminate within cleanup window")


def join_run(out, cfg):
    require(not (out / "observer-error.json").exists(), "native observer refused")
    owned, ready, finished = [
        read(out / name) for name in ("owned.json", "ready.json", "fixture-exited.json")
    ]
    require(
        finished == {"run": cfg["run"], "exitCode": 0}
        and ready["run"] == cfg["run"]
        and (ready["browserPid"], ready["gpuPid"]) == (owned["browserPid"], owned["gpuPid"]),
        "inconsistent process lifetimes",
    )
    native = [json.loads(l) for l in (out / "native.jsonl").read_text().splitlines()]
    require(
        native and native[0].get("controllerRun") == cfg["run"],
        "native log belongs to another run",
    )
    events, complete, scope = [read(out / ("bounded." + f + ".json")) for f in ("trace", "complete", "scope")]
    outcome = read(out / "outcome.json")
    require(
        outcome.get("diagnosticOnly") is True
        and outcome.get("verified") is True
        and outcome.get("knownEffects") == 1
        and outcome.get("committedRevision") == "B-r2"
        and outcome.get("imageCorrect") is True
        and outcome.get("nativeScopeRequest") == scope,
        "correct verified effect and the captured scope must agree",
    )
    control = outcome.get("mainWorldControl")
    if cfg.get("lookalike"):
        require(
            isinstance(control, dict)
            and control.get("otherTrackLive") is True
            and control.get("copiedLabelSubmitted") is True
            and control.get("privateCaptureVisible") is False,
            "requested main-world fault control did not complete",
        )
    tools = runpy.run_path(str(CORRELATOR))
    common = dict(
        browser_pid=owned["browserPid"],
        gpu_pid=owned["gpuPid"],
        frame=scope["frameId"],
        loader=scope["loaderId"],
    )
    doc = tools["correlate"](
        events["traceEvents"],
        native,
        tracing_complete=complete,
        scope_sha256=cfg["scopeImage"]["sha256"],
        doe_sha256=cfg["doeLibrary"]["sha256"],
        **common,
    )
    command = tools["capture_submission"](
        events["traceEvents"], doc["matchedDocumentSubmissions"], scope, **common
    )
    return {
        "status": "verified",
        "latencyQualified": False,
        "captureSubmission": command,
        "mainWorldControl": control,
        "ownedProcesses": owned,
        "applicationHttpRequests": outcome.get("applicationHttpRequests"),
        "knownEffects": 1,
        "plannerCalls": outcome.get("plannerCalls"),
        "modelSpendUsd": outcome.get("modelSpendUsd"),
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("preflight", "run"))
    p.add_argument("--profile", type=Path, required=True)
    p.add_argument(
        "--scope-image",
        type=Path,
        help="separately pinned browser DSO containing the scope, for component builds",
    )
    p.add_argument("--out", type=Path, required=True, help="fresh private output directory")
    p.add_argument("--timeout", type=int, default=240, choices=range(30, 1801), metavar="30..1800")
    p.add_argument(
        "--lookalike",
        action="store_true",
        help="request a separate fixture capture for the spoof control",
    )
    args = p.parse_args()
    os.umask(0o077)
    out = args.out.resolve()
    out.mkdir(mode=0o700, parents=False, exist_ok=False)
    deadline = time.monotonic() + args.timeout
    result = {
        "status": "refused",
        "latencyQualified": False,
        "hardwareQualified": False,
    }
    spawned = False
    try:
        pins = preflight(args.profile, args.scope_image, deadline)
        result["artifacts"] = pins
        if args.mode == "preflight":
            result.update(status="preflight-passed", browserStarted=False)
        else:
            require(shutil.which("node"), "Node is required")
            require(
                ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) == 0,
                "cannot own orphaned fixture children",
            )
            run = uuid.uuid4().hex
            cfg = {
                "run": run,
                "lookalike": args.lookalike,
                "deadline": deadline,
                "node": shutil.which("node"),
                **pins,
            }
            write(out / "run.json", cfg)
            # Keep desktop and driver discovery intact. An existing Node preload
            # or a test-run override may not replace the controlled browser.
            env = {
                k: v
                for k, v in os.environ.items()
                if not k.startswith(("BLITZEN_", "FAWN_WORKSPACE_"))
                and k
                not in (
                    "NODE_OPTIONS",
                    "NODE_PATH",
                    "LD_PRELOAD",
                    "LD_AUDIT",
                    "PYTHONPATH",
                    "PYTHONHOME",
                )
            }
            env.update(
                BLITZEN_CAPTURE_RUN_DIRECTORY=str(out),
                BLITZEN_CAPTURE_TIMEOUT_MS=str(args.timeout * 1000),
                BLITZEN_NATIVE_TRACE_RUN_ID=run,
                BLITZEN_CAPTURE_LAUNCH_SCRIPT=str(Path(__file__).resolve()),
                BLITZEN_RESIDENT_SCOPE_PROBE="1",
                BLITZEN_TEST_ELEMENT_CAPTURE="1",
                BLITZEN_TEST_BROWSER_CONFIG=str(args.profile.resolve()),
                BLITZEN_REVIEW_COMPOSITOR_TRANSPORT="external-texture",
                BLITZEN_RESIDENT_PROGRESS_FILE=str(out / "progress.json"),
                BLITZEN_RESIDENT_SCOPE_TRACE_PREFIX=str(out / "bounded"),
                BLITZEN_RESIDENT_RECEIPT_FILE=str(out / "outcome.json"),
                BLITZEN_NATIVE_TRACE_OUT=str(out / "native.jsonl"),
                BLITZEN_NATIVE_TRACE_LIBRARY=pins["doeLibrary"]["path"],
                BLITZEN_NATIVE_TRACE_SHA256=pins["doeLibrary"]["sha256"],
                BLITZEN_NATIVE_TRACE_SCOPE_IMAGE=pins["scopeImage"]["path"],
                BLITZEN_NATIVE_TRACE_SCOPE_SHA256=pins["scopeImage"]["sha256"],
            )
            if args.lookalike:
                env["BLITZEN_RESIDENT_SCOPE_LOOKALIKE"] = "1"

            def interrupted(*unused):
                raise InterruptedError("operator stopped the private diagnostic")

            signal.signal(signal.SIGTERM, interrupted)
            with (out / "gdb.log").open("x") as log:
                debugger = subprocess.Popen(
                    gdb_base()
                    + [
                        "-ex",
                        "python import os, runpy; runpy.run_path(os.environ['BLITZEN_CAPTURE_LAUNCH_SCRIPT'], run_name='__gdb__')",
                    ],
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                spawned = True
                code = debugger.wait(timeout=max(1, deadline - time.monotonic()))
            if (out / "observer-error.json").exists():
                raise ValueError(read(out / "observer-error.json")["reason"])
            require(code == 0, "GDB could not finish the owned fixture")
            result.update(join_run(out, cfg))
    except (Exception, KeyboardInterrupt) as exc:
        result["reason"] = str(exc) or "interrupted"
    finally:
        if spawned:
            try:
                cleanup_owned()
            except Exception as error:
                result.update(status="refused", cleanupError=str(error))
        write(out / "result.json", result)
    print(json.dumps(result))
    return int(result["status"] == "refused")


if __name__ == "__gdb__":
    observe_fixture()
elif __name__ == "__main__":
    sys.exit(main())
