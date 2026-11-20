#!/usr/bin/env python3
"""Resolve the mask's renderer command to one concrete native-proc invocation inside its ordinary wire batch."""

import argparse
import json
import math
from pathlib import Path
import re
import runpy

MASK = runpy.run_path(str(Path(__file__).with_name("check-mask-readback.py")))
INPUT = MASK["INPUT"]
require = INPUT["require"]
single = INPUT["single"]


def wire_submit(events: list[dict], scope: dict, browser: int, gpu: int, command: dict) -> dict:
    require(
        type(gpu) is int and gpu > 1 and len({browser, command["renderer"], gpu}) == 3,
        "owned browser, renderer and GPU child are required",
    )

    def native(name, pid):
        result = [
            e
            for e in events
            if e.get("name") == "FawnResident" + name
            and INPUT["CATEGORY"] in e.get("cat", "").split(",")
            and e.get("ph") in ("i", "I")
            and e.get("pid") == pid
        ]
        require(
            all(type(e.get("ts")) in (int, float) and math.isfinite(e["ts"]) for e in result),
            "invalid native timestamp",
        )
        return result

    encoded = native("EncodedSubmit", command["renderer"])
    client = single(
        [
            e
            for e in encoded
            if e.get("tid") == command["thread"]
            and all(e["args"].get(k) == command[k] for k in ("command", "device", "context"))
        ],
        "encoded tile-mask command",
    )
    batch, offset = client["args"]["batch"], client["args"]["offset"]
    require(
        isinstance(batch, str)
        and re.fullmatch(r"[1-9][0-9]{0,19}", batch)
        and int(batch) < 2**64
        and type(offset) is int
        and 0 <= offset < 2**32,
        "invalid wire coordinates",
    )

    def at_position(e):
        return (
            e["args"].get("batch") == batch
            and type(e["args"].get("offset")) is int
            and e["args"]["offset"] == offset
        )

    single([e for e in encoded if at_position(e)], "renderer ownership of wire position")
    decoded = native("DecodedSubmit", gpu)
    server = single([e for e in decoded if at_position(e)], "GPU native submit at matching wire offset")
    submission, binding = server["args"]["submission"], server["args"]["binding"]
    require(
        all(
            isinstance(token, str) and re.fullmatch("[A-F0-9]{32}", token)
            for token in (submission, binding)
        ),
        "invalid native submission or browser binding",
    )
    single([e for e in decoded if e["args"].get("submission") == submission], "GPU submission identity")
    require(
        all(command["start"] <= e["ts"] <= command["end"] for e in (client, server)),
        "submit outside this selected observation",
    )
    document = single(
        [e for e in native("DocumentValidated", browser) if e["args"].get("binding") == binding],
        "browser-validated document",
    )
    require(
        tuple(document["args"].get(k) for k in ("frame", "loader", "rendererPid"))
        == (scope["frameId"], scope["loaderId"], command["renderer"])
        and document["ts"] <= server["ts"],
        "wire submit belongs to a different document",
    )
    require(
        not any(
            e["args"].get("binding") == binding and e["ts"] <= command["end"]
            for e in native("DocumentRetired", browser)
        ),
        "document retired before readback",
    )
    return dict(
        nativeQueueSubmitInvoked=True,
        wireBatch=batch,
        wireOffset=offset,
        submission=submission,
        documentBinding=binding,
        gpuPid=gpu,
        nativeBackendCompletionVerified=False,
        latencyQualified=False,
    )


def check(row: Path) -> dict:
    events, scope, browser = INPUT["read_row"](row)
    _, command = MASK["traced_mask"](events, scope, browser)
    progress = json.loads((row / "progress.json").read_text())
    return wire_submit(events, scope, browser, progress["gpuProcessPid"], command)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("row", type=Path)
    options = parser.parse_args()
    try:
        print(json.dumps(check(options.row)))
    except (OSError, TypeError, ValueError, KeyError) as error:
        parser.exit(1, f"wire submit unproven: {error}\n")
