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


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--browser-trace", required=True, type=Path)
    p.add_argument("--tracing-complete", required=True, type=Path, help="CDP Tracing.tracingComplete params from the same trace")
    p.add_argument("--native-log", required=True, type=Path)
    for key in ("browser-pid", "gpu-pid"):
        p.add_argument("--" + key, type=int, required=True)
    for key in ("frame", "loader", "scope-sha256", "doe-sha256"):
        p.add_argument("--" + key, required=True)
    args = vars(p.parse_args())
    events = json.loads(args.pop("browser_trace").read_text())["traceEvents"]
    args["tracing_complete"] = json.loads(args["tracing_complete"].read_text())
    rows = [json.loads(line) for line in args.pop("native_log").read_text().splitlines()]
    print(json.dumps(correlate(events, rows, **args)))


if __name__ == "__main__":
    main()
