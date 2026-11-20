#!/usr/bin/env python3
"""Check the imported capture input in one private fixture row. This cannot qualify GPU timing."""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path


CATEGORY = "disabled-by-default-gpu.dawn"


def require(value: bool, message: str) -> None:
    if not value:
        raise ValueError(message)


def single(rows: list[dict], description: str) -> dict:
    require(len(rows) == 1, "missing or ambiguous " + description)
    return rows[0]


def trace_input(events: list[dict], scope: dict, browser: int) -> None:
    require(type(browser) is int and browser > 1, "invalid browser PID")
    world, ordinal = scope["executionContextId"], scope["ordinal"]
    require(all(type(n) is int and n > 0 for n in (world, ordinal, scope["grantRevision"])),
            "invalid world, ordinal or grant revision")
    require(all(isinstance(scope[k], str) and re.fullmatch(r"[A-F0-9]{32}", scope[k])
                for k in ("frameId", "loaderId")), "invalid document identity")
    require(bool(scope["captureId"] and scope["grantId"]) and type(scope["mediaTimestampUs"]) is int,
            "missing selected capture")

    def rows(name: str) -> list[dict]:
        found = [event for event in events if CATEGORY in event.get("cat", "").split(",")
                 and event.get("ph") in ("i", "I") and event.get("name") == "FawnResident" + name]
        require(all(type(e["ts"]) in (float, int) and math.isfinite(e["ts"]) for e in found),
                "invalid native trace clock")
        return found

    imports = sorted((event for event in rows("FrameImported")
                      if tuple(event["args"].get(k) for k in ("context", "frame", "loader"))
                      == (world, scope["frameId"], scope["loaderId"])), key=lambda e: e["ts"])
    require(len(imports) >= ordinal, "selected observation has no imported frame")
    chosen = single([e for e in imports if e["args"].get("mediaTimestampUs") == scope["mediaTimestampUs"]],
                    "native frame timestamp")
    require(imports[ordinal - 1] is chosen, "import belongs to a different observation")
    renderer, at, sample = chosen["pid"], chosen["ts"], chosen["args"]
    require(type(renderer) is int and renderer > 1 and renderer != browser and
            all(e["pid"] == renderer for e in imports), "renderer identity changed")
    restriction = single([e for e in rows("TrackRestriction") if e["pid"] == renderer and
                          e["args"].get("context") == world and e["ts"] <= at], "restricted track")
    track = restriction["args"]
    require(all(type(sample.get(k)) is int and type(track.get(k)) is int and sample[k] == track[k]
                for k in ("component", "sourceEpoch", "version")) and
            sample["component"] > 0 and sample["sourceEpoch"] >= 0 and sample["version"] > 0,
            "actual wrapper or captured frame version differs")
    require(bool(re.fullmatch(r"[A-F0-9]{32}", track["track"])) and
            all(re.fullmatch(r"[A-F0-9]{40}", track[k]) for k in ("stream", "target")),
            "missing browser capture identities")
    permit = single([e for e in rows("BrowserRestriction") if e["pid"] == browser
                     and e["args"].get("stream") == track["stream"] and e["ts"] <= at],
                    "browser restriction on this stream")
    require(restriction["ts"] <= permit["ts"] < at and permit["args"].get("accepted") is True
            and permit["args"].get("restriction") is True and
            all(permit["args"].get(k) == track[k] for k in ("target", "version")),
            "browser did not accept the matching restriction before import")
    require(not any(e["pid"] == browser and e["args"].get("stream") == track["stream"] and e["ts"] <= at
                    for e in rows("StreamClosed")), "browser stream closed or switched")


def check(row: Path) -> dict:
    def read(name: str) -> dict:
        return json.loads((row / (name + ".json")).read_text())

    source, progress, outcome, exit_record = map(read, ("browser-source", "progress", "outcome", "exit"))
    require(exit_record.get("allReady") is True and exit_record.get("exit") == 0 and
            not exit_record.get("waveTimeout") and outcome.get("verified") is True and
            outcome.get("diagnosticKind") == "frame-import" and outcome.get("diagnosticOnly") is True,
            "ordinary frame fixture did not finish")
    require(bool(re.fullmatch(r"[a-f0-9]{64}", source["chromeSha256"])) and
            progress["selection"]["executable"]["sha256"] == source["chromeSha256"],
            "missing process-bound Fawn image")
    require(read("frame-import.complete").get("dataLossOccurred") is False, "incomplete CDP trace")
    scope = read("frame-import.scope")
    expected = dict(progress["frameImportScope"], ordinal=outcome["frameImportOrdinal"],
                    mediaTimestampUs=outcome["frameImportTimestampUs"])
    require(expected == scope and scope["captureId"] == progress["surfaceId"], "controller scopes differ")
    trace_input(read("frame-import.trace")["traceEvents"], scope, progress["browserPid"])
    return {"restrictedFrameImportVerified": True, "nativeDispatchVerified": False,
            "latencyQualified": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("row", type=Path)
    arguments = parser.parse_args()
    try:
        print(json.dumps(check(arguments.row)))
    except (OSError, TypeError, KeyError, ValueError) as error:
        parser.exit(1, f"unproven frame import: {error}\n")
