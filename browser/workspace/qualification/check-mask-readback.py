#!/usr/bin/env python3
"""Join a selected restricted frame to the submitted tile-mask commands and their observed WebGPU read map."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import runpy

INPUT = runpy.run_path(str(Path(__file__).with_name("check-frame-import.py")))
require = INPUT["require"]


class Commands:
    """Consume one native device's sequential events in the fixture's supported command shape."""

    def __init__(self, events: list[dict], frame: dict):
        tag = frame["args"]
        self.seq = tag["seq"]
        self.time = frame["ts"]
        # Include frame boundaries so another sample cannot supply a missing edge.
        self.edges = {}
        for event in events:
            args = event.get("args", {})
            if (
                event.get("name") not in ("FawnResidentResourceEdge", "FawnResidentFrameImported")
                or INPUT["CATEGORY"] not in event.get("cat", "").split(",")
                or event.get("ph") not in ("i", "I")
                or event.get("pid") != frame["pid"]
                or args.get("context") != tag["context"]
                or args.get("device") != tag["device"]
            ):
                continue
            seq = args.get("seq")
            require(
                type(seq) is int and 0 < seq < 2**53 and seq not in self.edges,
                "duplicate or malformed device sequence",
            )
            self.edges[seq] = event
        require(
            type(self.seq) is int and self.edges.get(self.seq) is frame,
            "selected native frame lacks a unique device sequence",
        )
        self.tid = frame.get("tid")
        require(type(self.tid) is int and self.tid > 0, "missing issuing renderer thread")

    def take(self, op: str, owner: int | None = None, *expected: int | None) -> tuple[int, ...]:
        self.seq += 1
        event = self.edges.get(self.seq)
        require(
            event is not None
            and event["name"] == "FawnResidentResourceEdge"
            and event.get("tid") == self.tid,
            f"missing or interleaved {op}",
        )
        at = event.get("ts")
        require(
            type(at) in (int, float) and math.isfinite(at) and at >= self.time,
            "nonmonotonic or missing native edge timestamp",
        )
        self.time = at
        args = event["args"]
        values = tuple(args.get(k) for k in ("owner", "a", "b", "c", "d"))
        require(
            args.get("op") == op
            and all(type(x) is int and 0 <= x < 2**53 for x in values)
            and values[0] > 0
            and all(want is None or got == want for got, want in zip(values, (owner, *expected))),
            f"unexpected resource or arguments for {op}",
        )
        return values

    def texture_view(self) -> tuple[int, int]:
        texture, view, *_ = self.take("view")
        require(view > 0, "missing texture view")
        return texture, view

    def dispatch(self, gpu_pass: int, group: int, x: int, y: int) -> None:
        self.take("set-group", gpu_pass, 0, group, 0)
        self.take("dispatch", gpu_pass, x, y, 1)
        self.take("end", gpu_pass)

    def submit(self, encoder: int) -> tuple[int, int]:
        _, command, *_ = self.take("finish", encoder)
        require(command > 0, "missing finished command")
        queue, *_ = self.take("submit", None, command, 1)
        return queue, command

    def import_texture(self, external: int, x: int, y: int) -> tuple[int, int, int]:
        encoder, gpu_pass, *_ = self.take("pass")
        self.take("set-kernel", gpu_pass, 1)  # exact public conversion WGSL and main entry
        texture, view = self.texture_view()
        group, *_ = self.take("group", None, 2)
        self.take("external", group, 0, external)
        self.take("texture", group, 1, view)
        self.dispatch(gpu_pass, group, x, y)
        queue, command = self.submit(encoder)
        return texture, queue, command


def traced_mask(events: list[dict], scope: dict, browser: int) -> tuple[dict, dict]:
    frame = INPUT["trace_input"](events, scope, browser)
    tag = frame["args"]
    require(scope["ordinal"] > 1, "diff needs an earlier captured texture")
    require(
        all(type(tag.get(k)) is int and 0 < tag[k] < 2**53 for k in ("external", "device", "seq")),
        "frame input lacks a native external-texture or device identity",
    )
    earlier = [
        e
        for e in events
        if e.get("name") == "FawnResidentFrameImported"
        and INPUT["CATEGORY"] in e.get("cat", "").split(",")
        and e.get("ph") in ("i", "I")
        and e.get("pid") == frame["pid"]
        and type(e.get("args", {}).get("seq")) is int
        and e["args"]["seq"] < tag["seq"]
        and e["args"].get("device") == tag["device"]
        and e["args"].get("context") == tag["context"]
    ]
    require(bool(earlier), "no earlier capture on the same device and world")
    prior = max(earlier, key=lambda e: e["args"]["seq"])
    old = prior["args"]
    require(
        all(old.get(k) == tag[k] for k in ("frame", "loader", "component", "version", "sourceEpoch"))
        and prior.get("tid") == frame.get("tid")
        and prior["ts"] < frame["ts"]
        and old.get("external") != tag["external"],
        "earlier texture belongs to a different source",
    )
    old_scope = dict(scope, ordinal=scope["ordinal"] - 1, mediaTimestampUs=old["mediaTimestampUs"])
    require(
        INPUT["trace_input"](events, old_scope, browser) is prior,
        "earlier selected capture did not verify",
    )

    w, h = scope["width"], scope["height"]
    require(
        all(type(n) is int and n > 0 for n in (w, h)) and w * h <= 4 * 1024 * 1024,
        "invalid fixture dimensions",
    )
    x, y = (w + 7) // 8, (h + 7) // 8
    tile = 64
    while ((w + tile - 1) // tile) * ((h + tile - 1) // tile) > 256:
        tile *= 2
    size = ((((w + tile - 1) // tile) * ((h + tile - 1) // tile) + 31) // 32) * 4

    prior_commands = Commands(events, prior)
    previous_texture, old_queue, old_command = prior_commands.import_texture(old["external"], x, y)
    require(prior_commands.time <= frame["ts"], "earlier conversion overlaps selected import")
    commands = Commands(events, frame)
    current_texture, queue, import_command = commands.import_texture(tag["external"], x, y)
    require(
        queue == old_queue and current_texture != previous_texture,
        "resident texture or queue was substituted",
    )
    _, uniform, *_ = commands.take("write", queue, None, 0, 4)
    encoder, gpu_pass, *_ = commands.take("pass")
    commands.take("set-kernel", gpu_pass, 2)  # exact public tile comparison WGSL and compare entry
    earlier_texture, earlier_view = commands.texture_view()
    later_texture, later_view = commands.texture_view()
    require(
        (earlier_texture, later_texture) == (previous_texture, current_texture),
        "changed-tile dispatch did not bind the two imported textures",
    )
    group, *_ = commands.take("group", None, 4)
    commands.take("texture", group, 0, earlier_view)
    commands.take("texture", group, 1, later_view)
    _, _, dirty, *_ = commands.take("buffer", group, 2, None, 0, 0)
    commands.take("buffer", group, 3, uniform, 0, 0)
    commands.dispatch(gpu_pass, group, x, y)
    _, _, readback, *_ = commands.take("copy", encoder, dirty, None, size, 0)
    require(
        min(dirty, uniform, readback) > 0 and len({dirty, uniform, readback}) == 3,
        "output/uniform/readback buffer aliases",
    )
    seen_queue, diff_command = commands.submit(encoder)
    require(
        seen_queue == queue and len({old_command, import_command, diff_command}) == 3,
        "diff command or submitting queue aliases another",
    )
    commands.take("map", readback, 1, 0, size)
    commands.take("map-ok", readback)
    track = INPUT["single"](
        [
            e
            for e in events
            if e.get("name") == "FawnResidentTrackRestriction"
            and INPUT["CATEGORY"] in e.get("cat", "").split(",")
            and e.get("ph") in ("i", "I")
            and e.get("pid") == frame["pid"]
            and e.get("args", {}).get("context") == tag["context"]
            and e["ts"] <= frame["ts"]
        ],
        "native track",
    )
    invalidation = [
        e
        for e in events
        if e.get("name") in ("FawnResidentStreamClosed", "FawnResidentBrowserRestriction")
        and INPUT["CATEGORY"] in e.get("cat", "").split(",")
        and e.get("ph") in ("i", "I")
        and e.get("pid") == browser
        and e.get("args", {}).get("stream") == track["args"]["stream"]
        and frame["ts"] < e["ts"] <= commands.time
    ]
    require(not invalidation, "capture closed or restriction changed before successful readback")
    # These private native identities let the wire checker select exactly this
    # mask command. The existing frame-only and mask-only outputs stay stable.
    command_scope = dict(
        command=diff_command,
        renderer=frame["pid"],
        thread=frame["tid"],
        context=tag["context"],
        device=tag["device"],
        start=frame["ts"],
        end=commands.time,
    )
    return {
        "restrictedFrameImportVerified": True,
        "submittedTileDiffVerified": True,
        "webGpuMaskReadMapSucceeded": True,
        "maskBytes": size,
        "nativeBackendCompletionVerified": False,
        "latencyQualified": False,
    }, command_scope


def checked_mask(events: list[dict], scope: dict, browser: int) -> dict:
    result, _ = traced_mask(events, scope, browser)
    return result


def check(row: Path) -> dict:
    return checked_mask(*INPUT["read_row"](row))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("row", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.row)))
    except (OSError, TypeError, KeyError, ValueError) as error:
        parser.exit(1, f"unproven mask readback: {error}\n")
