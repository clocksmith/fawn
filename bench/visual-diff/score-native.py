"""Check the frozen native diff protocol and score its complete samples."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
from typing import Any


def score(data: dict[str, Any]) -> dict[str, Any]:
    """Compute nearest-rank quantiles only for complete, oracle-checked arms."""
    if data["failures"] or not data["windowOwned"] or data["modelLoaded"]:
        raise ValueError(f"Cannot score a failed or ineligible comparison: {data['failures']}")
    if (data["width"], data["height"], data["format"]) != (512, 256, "BGRA8 sRGB"):
        raise ValueError("Unexpected workload format")
    results = {}
    for arm in ("cpu", "dawn", "doe_c"):
        samples = [s for s in data["timing"] if s["arm"] == arm]
        if Counter(s["changed"] for s in samples) != {True: 64, False: 64}:
            raise ValueError(f"{arm} requires 64 equal pairs and 64 localized changes")
        for index, sample in enumerate(samples):
            if (sample["changed"] != bool(index % 2) or
                sample["mask"] != (1024 if sample["changed"] else 0) or sample["elapsedNs"] <= 0):
                raise ValueError(f"{arm} sample {index} disagrees with the protocol")
        times = sorted(s["elapsedNs"] for s in samples)
        results[arm] = {f"p{q}": times[math.ceil(len(times)*q/100) - 1] for q in (50, 95, 99)}
    doe = results["doe_c"]
    passed = all(doe["p95"] * 100 <= results[c]["p95"] * 90 and
                 doe["p99"] * 100 <= results[c]["p99"] * 105 for c in ("cpu", "dawn"))
    return {"units": "nanoseconds", "percentiles": results, "doeThresholdMet": passed,
            "samplesPerArm": 128, "cpuCopiedBytesPerPair": 1_048_576,
            "gpuResultMappedBytesPerPair": 4, "driverTransferCounts": None}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    args = parser.parse_args()
    print(json.dumps(score(json.loads(args.receipt.read_text())), indent=2))
