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
from bench.runners import run_release_scoreboard as runner  # noqa: E402
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

    def test_capability_mode_must_match_the_declared_mode(self) -> None:
        scoreboard = {**SCOREBOARD, "backends": [{"backend": "metal", "capabilityMode": "intersection"}]}
        summary = _summary(self.directory, {"w_a": True, "w_b1": True, "w_b2": True})
        summary["capabilityMode"] = "full"
        self.assertEqual(gate.blocking_reasons(scoreboard, summary, "metal"),
                         ["summary capability mode 'full' is not the declared 'intersection' for metal"])
        summary["capabilityMode"] = "intersection"
        self.assertEqual(gate.blocking_reasons(scoreboard, summary, "metal"), [])

    def test_not_comparable_on_host_blocks_with_its_reason(self) -> None:
        summary = _summary(self.directory, {"w_a": True, "w_b1": True, "w_b2": True})
        entry = summary["rows"][0]["workloads"][0]
        entry.update(status="not-comparable-on-host", reasons=["not comparable on this host: needs f16"])
        self.assertEqual(gate.blocking_reasons(SCOREBOARD, summary, "metal"),
                         ["row-a/w_a: not-comparable-on-host: not comparable on this host: needs f16"])


class CapabilityIntersectionTest(unittest.TestCase):
    def test_intersection_keeps_shared_features_and_less_capable_limits(self) -> None:
        probes = {
            "doe": {"features": ["shader-f16", "subgroups", "timestamp-query"],
                    "limits": {"maxBufferSize": 2147483648, "minStorageBufferOffsetAlignment": 32,
                               "maxImmediateSize": 64}},
            "dawn": {"features": ["timestamp-query", "texture-compression-bc"],
                     "limits": {"maxBufferSize": 268435456, "minStorageBufferOffsetAlignment": 256}},
        }
        self.assertEqual(runner.intersect_capabilities(probes), {
            "features": ["timestamp-query"],
            "limits": {"maxBufferSize": 268435456, "minStorageBufferOffsetAlignment": 256},
        })

    def test_full_mode_carries_no_capability_set(self) -> None:
        env = runner.capability_environment("full", None)
        self.assertEqual(env[runner.CAPABILITY_MODE_ENV], "full")
        self.assertNotIn(runner.CAPABILITY_SET_ENV, env)
        env = runner.capability_environment("intersection", {"features": ["a"], "limits": {"maxX": 1}})
        self.assertEqual(json.loads(env[runner.CAPABILITY_SET_ENV]), {"features": ["a"], "limits": {"maxX": 1}})

    def test_failures_are_labeled_only_in_intersection_mode(self) -> None:
        products = {"baseline": "doe", "comparison": "dawn"}

        def failure(message: str, output_check: bool = False) -> dict:
            return {"message": message, "outputCheck": output_check}

        same = {"baseline": failure("needs f16"), "comparison": failure("needs f16")}
        self.assertEqual(runner.classify_failure("intersection", products, same),
                         ("not-comparable-on-host", ["not comparable on this host: needs f16"]))
        self.assertIsNone(runner.classify_failure("full", products, same))
        differ = {"baseline": failure("doe crashed"), "comparison": failure("needs f16")}
        self.assertIsNone(runner.classify_failure("intersection", products, differ))
        dawn = {"baseline": None, "comparison": failure("FXC compile failed")}
        self.assertEqual(runner.classify_failure("intersection", products, dawn),
                         ("dawn-limitation", ["Dawn limitation on this host: FXC compile failed"]))
        unreferenced = {"baseline": failure("no recorded reference", True), "comparison": failure("FXC failed")}
        status, reasons = runner.classify_failure("intersection", products, unreferenced)
        self.assertEqual(status, "dawn-limitation")
        self.assertEqual(reasons[1], "doe executed; its output check: no recorded reference")
        doe = {"baseline": failure("doe failed"), "comparison": None}
        self.assertIsNone(runner.classify_failure("intersection", products, doe))
        # Matching output-check failures executed on both sides: not a capability refusal.
        checks = {"baseline": failure("no reference", True), "comparison": failure("no reference", True)}
        self.assertIsNone(runner.classify_failure("intersection", products, checks))


class ProviderOrderAndHostStateTest(unittest.TestCase):
    PRODUCTS = [("baseline", "doe"), ("comparison", "dawn")]

    def test_first_provider_leads_and_order_alternates_by_workload(self) -> None:
        order = lambda index, first: [product for _, product in runner.provider_order(self.PRODUCTS, index, first)]
        self.assertEqual(order(0, ""), ["doe", "dawn"])
        self.assertEqual(order(1, ""), ["dawn", "doe"])
        self.assertEqual(order(0, "dawn"), ["dawn", "doe"])
        self.assertEqual(order(1, "dawn"), ["doe", "dawn"])
        self.assertEqual(order(0, "doe"), ["doe", "dawn"])
        self.assertEqual(runner.provider_order(self.PRODUCTS, 0, "dawn")[0], ("comparison", "dawn"))

    def _snapshot(self, load: float, cpu: float) -> dict:
        return {"loadAverage": [load, load, load],
                "busiestProcesses": [{"pid": 7, "cpuPercent": cpu, "command": "Google Chrome Helper"}]}

    def test_quiet_wait_polls_until_load_and_processes_settle(self) -> None:
        states = iter([self._snapshot(5.0, 10.0), self._snapshot(2.0, 90.0), self._snapshot(2.0, 10.0)])
        now = [0.0]
        sleeps = []

        def sleep(seconds: float) -> None:
            sleeps.append(seconds)
            now[0] += seconds

        result = runner.wait_for_quiet_host(3.0, 50.0, 600.0, clock=lambda: now[0], sleep=sleep,
                                            snapshot_fn=lambda: next(states))
        self.assertEqual(len(sleeps), 2)
        self.assertTrue(result["quiet"])
        self.assertEqual(result["waitedSeconds"], 30.0)
        self.assertEqual(result["notQuietReasons"], [])

    def test_quiet_wait_times_out_and_records_why(self) -> None:
        now = [0.0]

        def sleep(seconds: float) -> None:
            now[0] += seconds

        result = runner.wait_for_quiet_host(3.0, 50.0, 30.0, clock=lambda: now[0], sleep=sleep,
                                            snapshot_fn=lambda: self._snapshot(8.0, 10.0))
        self.assertFalse(result["quiet"])
        self.assertEqual(result["notQuietReasons"], ["1-min load 8.00 >= 3.0"])

    def test_without_a_limit_the_snapshot_is_only_recorded(self) -> None:
        result = runner.wait_for_quiet_host(0.0, 50.0, 30.0, sleep=lambda _: self.fail("waited"),
                                            snapshot_fn=lambda: self._snapshot(8.0, 99.0))
        self.assertEqual((result["quietRequired"], result["quiet"]), (False, True))


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

    def test_different_capability_sets_cannot_record_a_reference(self) -> None:
        full = {**self._meta("doe", [1, 2]), "capability": {"mode": "full"}}
        shared = {**self._meta("node-webgpu", [1, 2]),
                  "capability": {"mode": "intersection", "exposedFeatures": [], "exposedLimits": {}}}
        with self.assertRaises(ValueError):
            record.token_reference([full, shared])

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
