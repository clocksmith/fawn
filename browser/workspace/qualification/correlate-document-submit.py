"""Correlate diagnostic files captured independently from a private browser and its GPU child."""

import argparse
import json
import math
import re
from pathlib import Path

TOKEN = re.compile("[A-F0-9]{32}")
CATEGORY = "disabled-by-default-gpu.dawn"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def correlate(events, native, *, tracing_complete, browser_pid, gpu_pid, frame, loader, scope_sha256, doe_sha256):
    require(tracing_complete.get("dataLossOccurred") is False, "lossless CDP tracingComplete required")
    require(browser_pid > 0 and gpu_pid > 0 and browser_pid != gpu_pid, "distinct owned process IDs required")
    require(TOKEN.fullmatch(frame) and TOKEN.fullmatch(loader), "expected DevTools frame and loader tokens")
    require(all(re.fullmatch("[0-9a-f]{64}", pin) for pin in (scope_sha256, doe_sha256)), "missing artifact pin")
    require(len(native) >= 2, "incomplete collector stream")
    start, end = native[0], native[-1]
    require(start.get("event") == "collector-armed" and start.get("pid") == gpu_pid and
            start.get("scopeImageSha256") == scope_sha256 and start.get("nativeLibrarySha256") == doe_sha256,
            "collector process or loaded image differs")
    require(end.get("event") == "collector-ended" and end.get("exitCode") == 0 and
            end.get("errorCount") == 0 and end.get("unresolved") == [], "native capture incomplete")
    documents, retired, submissions = {}, set(), {}
    for event in events:
        name, pid = event.get("name"), event.get("pid")
        if CATEGORY not in event.get("cat", "").split(",") or event.get("ph") not in ("I", "i", "B", "X"):
            continue
        if pid != (gpu_pid if name == "FawnResidentWireSubmit" else browser_pid):
            continue
        if name not in ("FawnResidentDocumentValidated", "FawnResidentDocumentRetired", "FawnResidentWireSubmit"):
            continue
        args = event["args"]
        binding, ts = args["binding"], event["ts"]
        require(TOKEN.fullmatch(binding) and math.isfinite(ts), "malformed binding or timestamp")
        if name == "FawnResidentDocumentRetired":
            # Include only complete, separately bracketed observation windows.
            # Lacking a shared native/Perfetto clock, even a late retirement disqualifies this binding.
            retired.add(binding)
        elif name == "FawnResidentDocumentValidated":
            identity = args["frame"], args["loader"]
            require(all(TOKEN.fullmatch(t) for t in identity), "malformed document identity")
            previous = documents.get(binding)
            require(previous is None or previous[0] == identity, "binding reused by a different document")
            documents[binding] = (identity, ts if previous is None else min(previous[1], ts))
        else:
            command = args["submission"]
            require(TOKEN.fullmatch(command) and command not in submissions, "ambiguous wire submit")
            submissions[command] = binding, ts

    scopes, used_commands, completed = {}, set(), set()
    result = []
    for row in native[1:-1]:
        event = row["event"]
        require(event not in ("collector-armed", "collector-ended"), "multiple native collector lifetimes")
        if event == "trusted-wire-scope":
            command = row["wireSubmission"]
            key = row["submit"], row["queue"]
            require(key not in scopes and command not in used_commands, "duplicate native scope")
            used_commands.add(command)
            scopes[key] = row["documentBinding"], command
        elif event == "native-fence-observed":
            key = row["submit"], row["queue"]
            if key not in scopes:
                continue
            binding, command = scopes[key]
            require(key not in completed, "more than one compute completion for one wire submit")
            completed.add(key)
            document, birth = documents.get(binding, (None, None))
            wire = submissions.get(command)
            if document != (frame, loader) or binding in retired or not wire:
                continue
            require(wire[0] == binding and wire[1] >= birth, "wire activity outside its document lifetime")
            result.append(command)
    return {"matchedDocumentSubmissions": result, "authorizedSampleProven": False, "latencyQualified": False}


def capture_submission(
    events, matched_commands, request, *, browser_pid, gpu_pid, frame, loader
):
    # The controller supplies its actual receipt from the grant-owning adapter.
    # Neither world names nor command labels participate in any lookup.
    require(
        request.get("frameId") == frame
        and request.get("loaderId") == loader
        and request.get("grantId")
        and request.get("grantRevision") is not None
        and request.get("captureId"),
        "missing controller-owned capture grant",
    )
    world, ordinal = request.get("executionContextId"), request.get("bracket")
    require(
        all(type(x) is int and x > 0 for x in (world, ordinal)),
        "invalid world or capture bracket",
    )

    def select(name, pid):
        return [
            e
            for e in events
            if e.get("pid") == pid
            and e.get("name") == "FawnResident" + name
            and CATEGORY in e.get("cat", "").split(",")
            and e.get("ph") in ("X", "I", "i")
        ]

    pids = {
        e["args"].get("rendererPid")
        for e in select("DocumentValidated", browser_pid)
        if (e["args"].get("frame"), e["args"].get("loader")) == (frame, loader)
    }
    require(len(pids) == 1, "missing or ambiguous renderer process")
    renderer_pid = pids.pop()
    require(
        type(renderer_pid) is int
        and renderer_pid > 0
        and renderer_pid not in (gpu_pid, browser_pid),
        "renderer process identity missing",
    )
    requests = [
        e
        for e in select("TrackRestriction", renderer_pid)
        if e["args"].get("context") == world
    ]
    # This private realm belongs to a single capture and restricts exactly once.
    # Even an additional track using the same underlying media session disqualifies it.
    require(len(requests) == 1, "additional track, stream or restriction in this world")
    source = requests[0]["args"]
    track, stream, target = source["track"], source["stream"], source["target"]
    require(
        TOKEN.fullmatch(track)
        and all(re.fullmatch("[0-9A-F]{40}", h) for h in (stream, target))
        and type(source["version"]) is int
        and source["version"] > 0,
        "invalid track or restricted session",
    )
    witnesses = sorted(
        (
            e
            for e in select("TrackState", renderer_pid)
            if e["args"].get("context") == world
        ),
        key=lambda e: e["ts"],
    )
    require(
        len(witnesses) >= ordinal * 2
        and all(
            e["args"]["track"] == track
            and e["args"]["stream"] == stream
            and e["args"]["live"] is True
            for e in witnesses
        ),
        "missing or ambiguous live track witnesses",
    )
    lo, hi = witnesses[2 * (ordinal - 1) : 2 * ordinal]
    start, end = lo["ts"], hi["ts"]
    require(start < end, "invalid capture witness interval")
    authority = [
        e
        for e in select("BrowserRestriction", browser_pid)
        if e["args"]["stream"] == stream and requests[0]["ts"] <= e["ts"] <= end
    ]
    require(
        len(authority) == 1,
        "missing browser grant or additional attempt to change its restriction",
    )
    permit = authority[0]
    args = permit["args"]
    require(
        args.get("accepted") is True
        and args.get("restriction") is True
        and args["target"] == target
        and args["version"] == source["version"]
        and permit["ts"] <= start,
        "browser did not confirm this active restriction",
    )
    require(
        not any(
            e["args"]["stream"] == stream and e["ts"] <= end
            for e in select("StreamClosed", browser_pid)
        ),
        "the selected browser capture session was closed or switched",
    )
    submits = [
        e
        for e in select("WorldSubmit", renderer_pid)
        if e["ph"] == "X"
        and e["args"]["context"] == world
        and start <= e["ts"]
        and e["dur"] >= 0
        and e["ts"] + e["dur"] <= hi["ts"]
    ]
    require(
        len(submits) == 1,
        "expected a single scoped Blink submit from the isolated world",
    )
    call = submits[0]
    batches = select("RendererBatch", renderer_pid)
    inner = [
        e["args"]["batch"]
        for e in batches
        if e["tid"] == call["tid"] and call["ts"] <= e["ts"] <= call["ts"] + call["dur"]
    ]
    require(
        len(inner) == 1 and re.fullmatch("[1-9][0-9]*", inner[0]),
        "scoped submit has no unique native Dawn batch",
    )
    require(
        sum(e["args"]["batch"] == inner[0] for e in batches) == 1,
        "renderer batch identity reused",
    )
    gpu = [e for e in select("GpuBatch", gpu_pid) if e["args"]["batch"] == inner[0]]
    require(
        len(gpu) == 1
        and gpu[0]["args"]["submission"] in matched_commands
        and call["ts"] <= gpu[0]["ts"] <= hi["ts"],
        "matching completed Doe command is absent or ambiguous",
    )
    return gpu[0]["args"]["submission"]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--browser-trace", required=True, type=Path)
    p.add_argument("--tracing-complete", required=True, type=Path, help="CDP Tracing.tracingComplete params from the same trace")
    p.add_argument("--scope-request", type=Path, help="nativeScopeRequest from the trusted workspace controller")
    p.add_argument("--native-log", required=True, type=Path)
    for key in ("browser-pid", "gpu-pid"):
        p.add_argument("--" + key, type=int, required=True)
    for key in ("frame", "loader", "scope-sha256", "doe-sha256"):
        p.add_argument("--" + key, required=True)
    args = vars(p.parse_args())
    events = json.loads(args.pop("browser_trace").read_text())["traceEvents"]
    args["tracing_complete"] = json.loads(args["tracing_complete"].read_text())
    rows = [json.loads(line) for line in args.pop("native_log").read_text().splitlines()]
    request_path = args.pop("scope_request")
    result = correlate(events, rows, **args)
    if request_path:
        request = json.loads(request_path.read_text())
        result["captureSubmission"] = capture_submission(events, result["matchedDocumentSubmissions"], request,
              **{k: args[k] for k in ("browser_pid","gpu_pid","frame","loader")})
        result["authorizedSampleProven"] = True
    print(json.dumps(result))


if __name__ == "__main__":
    main()
