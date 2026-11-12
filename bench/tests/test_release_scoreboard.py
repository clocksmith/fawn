#!/usr/bin/env python3
"""Release scoreboard: claim admission, output identity and reference custody."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

# The compare modules import themselves as top-level `native_compare_modules`.
BENCH_ROOT = Path(__file__).resolve().parents[1]
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from bench.gates import release_scoreboard_gate as gate  # noqa: E402
from native_compare_modules.compare_assessment_helpers import collect_result_output_signatures  # noqa: E402
from native_compare_modules.timing_selection import classify_timing_source  # noqa: E402
from bench.tools import prepare_release_scoreboard_models as prepare  # noqa: E402
from bench.tools import record_release_scoreboard_reference as record  # noqa: E402

SCOREBOARD = {
    "scoreboardVersion": "test-v1",
    "rows": [
        {"rowId": "row-a", "workloadIds": ["w_a"]},
        {"rowId": "row-b", "workloadIds": ["w_b1", "w_b2"]},
    ],
}


def _report(directory: Path, workload_id: str, comparable: bool) -> tuple[str, str]:
    path = directory / f"{workload_id}.compare.json"
    path.write_text(json.dumps({"workloads": [{
        "id": workload_id,
        "comparability": {"comparable": comparable, "reasons": [] if comparable else ["output unverified"]},
    }]}), encoding="utf-8")
    return str(path), hashlib.sha256(path.read_bytes()).hexdigest()


def _summary(directory: Path, comparable: dict[str, bool], functional_only: bool = False) -> dict:
    rows = []
    for row in SCOREBOARD["rows"]:
        entries = []
        for workload_id in row["workloadIds"]:
            if workload_id not in comparable:
                continue
            report, digest = _report(directory, workload_id, comparable[workload_id])
            entries.append({"workloadId": workload_id, "compareReport": report, "compareReportSha256": digest,
                            "status": "comparable" if comparable[workload_id] else "rejected"})
        rows.append({"rowId": row["rowId"], "workloads": entries})
    return {"backend": "metal", "scoreboardVersion": "test-v1", "functionalOnly": functional_only, "rows": rows}


class ReleaseScoreboardGateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = Path(tempfile.mkdtemp())

    def test_full_comparable_scoreboard_is_admissible(self) -> None:
        summary = _summary(self.directory, {"w_a": True, "w_b1": True, "w_b2": True})
        self.assertEqual(gate.blocking_reasons(SCOREBOARD, summary, "metal"), [])

    def test_unmeasured_workload_blocks_the_claim(self) -> None:
        summary = _summary(self.directory, {"w_a": True, "w_b1": True})
        self.assertEqual(gate.blocking_reasons(SCOREBOARD, summary, "metal"), ["row-b/w_b2: unmeasured"])

    def test_rejected_and_functional_runs_block_the_claim(self) -> None:
        summary = _summary(self.directory, {"w_a": False, "w_b1": True, "w_b2": True}, functional_only=True)
        reasons = gate.blocking_reasons(SCOREBOARD, summary, "metal")
        self.assertIn("row-a/w_a: rejected", reasons)
        self.assertTrue(any("functional-only" in reason for reason in reasons))

    def test_summary_status_cannot_override_the_report(self) -> None:
        summary = _summary(self.directory, {"w_a": True, "w_b1": True, "w_b2": True})
        entry = summary["rows"][0]["workloads"][0]
        Path(entry["compareReport"]).write_text(json.dumps({"workloads": [{
            "id": "w_a", "comparability": {"comparable": False, "reasons": ["edited"]}}]}), encoding="utf-8")
        self.assertEqual(gate.blocking_reasons(SCOREBOARD, summary, "metal"),
                         ["row-a/w_a: compare report changed after the run"])
        entry["compareReportSha256"] = hashlib.sha256(Path(entry["compareReport"]).read_bytes()).hexdigest()
        self.assertEqual(gate.blocking_reasons(SCOREBOARD, summary, "metal"), ["row-a/w_a: not comparable: edited"])

    def test_other_backend_summary_is_refused(self) -> None:
        summary = _summary(self.directory, {"w_a": True, "w_b1": True, "w_b2": True})
        self.assertIn("summary is for backend 'metal', not 'vulkan'",
                      gate.blocking_reasons(SCOREBOARD, summary, "vulkan"))


class ScoreboardOutputIdentityTest(unittest.TestCase):
    def _sample(self, digest: str, length: int) -> dict:
        return {"traceMeta": {"resultSummary": {"outputSha256": digest, "outputByteLength": length}}}

    def test_declared_output_digests_feed_result_equality(self) -> None:
        same = collect_result_output_signatures([self._sample("a" * 64, 6), self._sample("a" * 64, 6)])
        self.assertEqual(same, {("a" * 64, 6)})
        different = collect_result_output_signatures([self._sample("a" * 64, 6), self._sample("b" * 64, 6)])
        self.assertEqual(len(different), 2)

    def test_scoreboard_intervals_are_operation_timing(self) -> None:
        self.assertEqual(classify_timing_source("release-scoreboard-frame-p50-ms"), "operation")


class ReferenceRecordingTest(unittest.TestCase):
    def _meta(self, provider: str, tokens: list[int]) -> dict:
        return {"executionProvider": provider, "shaderSourceReceiptsHash": "s" * 64,
                "resultSummary": {"tokenIds": tokens, "promptTokenCount": 5}}

    def test_disagreeing_providers_cannot_record_a_reference(self) -> None:
        with self.assertRaises(ValueError):
            record.token_reference([self._meta("doe", [1, 2]), self._meta("node-webgpu", [1, 3])])

    def test_agreement_is_labeled_consensus(self) -> None:
        reference = record.token_reference([self._meta("doe", [1, 2]), self._meta("node-webgpu", [1, 2])])
        self.assertEqual(reference["referenceClass"], "recorded-cross-provider-consensus")
        single = record.token_reference([self._meta("node-webgpu", [1, 2])])
        self.assertEqual(single["referenceClass"], "recorded-single-provider-dawn")


class ModelPreparationTest(unittest.TestCase):
    def test_shard_bytes_must_match_the_tracked_manifest(self) -> None:
        root = Path(tempfile.mkdtemp())
        metadata = root / "doppler" / "models" / "local" / "m"
        metadata.mkdir(parents=True)
        shards = root / "shards"
        shards.mkdir()
        (shards / "shard_00000.bin").write_bytes(b"weights")
        manifest = {"shards": [{"filename": "shard_00000.bin", "size": 7,
                                "hash": hashlib.sha256(b"weights").hexdigest()}]}
        (metadata / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        custody = prepare.prepare("m", shards, root / "doppler", root / "out", copy=False)
        self.assertTrue(custody["allShardsVerified"])
        (shards / "shard_00000.bin").write_bytes(b"WEIGHTS")
        with self.assertRaises(ValueError):
            prepare.prepare("m", shards, root / "doppler", root / "out", copy=False)


if __name__ == "__main__":
    unittest.main()
