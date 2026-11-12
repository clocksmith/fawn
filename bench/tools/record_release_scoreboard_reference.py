"""Record a release-scoreboard output reference from executed receipts.

Token rows (Doppler generation) take one or more trace-meta receipts of the
same workload and backend. Every receipt must have executed the generation,
used the same shader receipt and produced identical token ids; agreement of
both providers is labeled consensus, a single provider is labeled as such.

Frame rows (three.js scene) take a raw frame dumped through
FAWN_SCOREBOARD_FRAME_DUMP_DIR plus its receipt; the frame digest must equal
the receipt's, and the frame is stored gzip-compressed beside the scenario
with the declared tolerance.

A reference records what a run produced; it is a drift and cross-host
control, not an independent numerical oracle.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
REFERENCE_DIR = REPO_ROOT / "bench" / "workloads" / "release-scoreboard" / "references"
RECORDABLE_FAILURES = ("has no recorded", "differ from the recorded", "against the recorded")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="kind", required=True)
    for name in ("tokens", "frame"):
        command = sub.add_parser(name, help=f"record a {name} reference")
        command.add_argument("--scenario", required=True, type=Path, help="Scenario JSON to update.")
        command.add_argument("--workload", required=True, help="Workload id inside the scenario.")
        command.add_argument("--backend", required=True, choices=["metal", "vulkan", "d3d12"],
                             help="Backend whose reference is recorded.")
        command.add_argument("--trace-meta", required=True, action="append", type=Path,
                             help="Executed receipt; repeat for each provider.")
    frame = sub.choices["frame"]
    frame.add_argument("--frame", required=True, type=Path, help="Raw RGBA frame dumped by the executor.")
    frame.add_argument("--max-channel-difference", type=int, required=True,
                       help="Largest per-channel difference a provider frame may show.")
    frame.add_argument("--max-differing-pixel-fraction", type=float, required=True,
                       help="Largest fraction of pixels that may differ at all.")
    return parser.parse_args(argv)


def _load_receipt(path: Path, workload: str, backend: str) -> dict[str, Any]:
    meta = json.loads(path.read_text(encoding="utf-8"))
    if meta.get("workloadId") != workload or meta.get("requestedBackend") != backend:
        raise ValueError(f"{path}: receipt is for {meta.get('workloadId')} on {meta.get('requestedBackend')}")
    failure = str(meta.get("failureMessage") or "")
    if failure and not any(marker in failure for marker in RECORDABLE_FAILURES):
        raise ValueError(f"{path}: execution failed before producing output: {failure}")
    if not isinstance(meta.get("resultSummary"), dict) or not meta.get("shaderSourceReceiptsHash"):
        raise ValueError(f"{path}: receipt has no result summary or shader receipt")
    return meta


def _provider(meta: dict[str, Any]) -> str:
    return "doe" if meta.get("executionProvider") == "doe" else "dawn"


def _recorded_with(metas: list[dict[str, Any]]) -> dict[str, Any]:
    first = metas[0]
    return {
        "providers": sorted({_provider(meta) for meta in metas}),
        "adapterDevice": (first.get("adapterInfo") or {}).get("device"),
        "shaderSourceReceiptsHash": first.get("shaderSourceReceiptsHash"),
        "dopplerCommit": (first.get("dopplerSource") or {}).get("dopplerCommit"),
        "modelManifestSha256": (first.get("modelCustody") or {}).get("manifestSha256"),
        "engine": first.get("engine"),
        "capability": _capability(first),
    }


def _capability(meta: dict[str, Any]) -> dict[str, Any]:
    """The capability mode and exposed set the receipt ran under."""
    record = meta.get("capability") or {}
    return {key: record.get(key) for key in ("mode", "exposedFeatures", "exposedLimits")}


def _reference_class(metas: list[dict[str, Any]]) -> str:
    providers = sorted({_provider(meta) for meta in metas})
    if providers == ["dawn", "doe"]:
        return "recorded-cross-provider-consensus"
    return f"recorded-single-provider-{providers[0]}"


def token_reference(metas: list[dict[str, Any]]) -> dict[str, Any]:
    """Return the token reference all receipts agree on."""
    signatures = {
        (
            json.dumps(meta["resultSummary"].get("tokenIds")),
            meta["resultSummary"].get("promptTokenCount"),
            meta.get("shaderSourceReceiptsHash"),
            json.dumps(_capability(meta), sort_keys=True),
        )
        for meta in metas
    }
    if len(signatures) != 1:
        raise ValueError("receipts disagree on tokens, prompt length, shaders or capability set: "
                         f"{sorted(signatures)}")
    summary = metas[0]["resultSummary"]
    if not summary.get("tokenIds"):
        raise ValueError("receipt carries no generated token ids")
    return {
        "tokenIds": summary["tokenIds"],
        "promptTokenCount": summary["promptTokenCount"],
        "referenceClass": _reference_class(metas),
        "recordedWith": _recorded_with(metas),
    }


def frame_reference(args: argparse.Namespace, metas: list[dict[str, Any]]) -> dict[str, Any]:
    """Store the dumped frame and return its reference with tolerance."""
    frame = args.frame.read_bytes()
    digest = hashlib.sha256(frame).hexdigest()
    owners = [meta for meta in metas if meta["resultSummary"].get("frameSha256") == digest]
    if not owners:
        raise ValueError("the dumped frame digest matches none of the given receipts")
    REFERENCE_DIR.mkdir(parents=True, exist_ok=True)
    target = REFERENCE_DIR / f"{args.workload}.{args.backend}.rgba.gz"
    target.write_bytes(gzip.compress(frame, compresslevel=9, mtime=0))
    return {
        "framePath": target.relative_to(REPO_ROOT).as_posix(),
        "frameSha256": digest,
        "referenceClass": _reference_class(owners),
        "tolerance": {
            "maxChannelDifference": args.max_channel_difference,
            "maxDifferingPixelFraction": args.max_differing_pixel_fraction,
        },
        "recordedWith": _recorded_with(owners),
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        scenario = json.loads(args.scenario.read_text(encoding="utf-8"))
        workload = next((entry for entry in scenario["workloads"] if entry["id"] == args.workload), None)
        if workload is None:
            raise ValueError(f"{args.scenario} has no workload {args.workload}")
        metas = [_load_receipt(path, args.workload, args.backend) for path in args.trace_meta]
        reference = token_reference(metas) if args.kind == "tokens" else frame_reference(args, metas)
    except (OSError, KeyError, ValueError) as exc:
        print(f"record failed: {exc}", file=sys.stderr)
        return 1
    workload.setdefault("references", {})[args.backend] = reference
    # LF on every host: Windows text mode would rewrite the tracked file as CRLF.
    args.scenario.write_text(json.dumps(scenario, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"workload": args.workload, "backend": args.backend,
                      "referenceClass": reference["referenceClass"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
