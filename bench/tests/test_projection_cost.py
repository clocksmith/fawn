"""Sensitivity checks for the distinct resident projection timing boundary."""
from __future__ import annotations

import copy
import unittest

from bench.executors.projection_cost import summarize, validate


class ProjectionCostTest(unittest.TestCase):
    def rows(self, mode: str = "none", transfer: str = "resident") -> list[dict]:
        phases = {"recordMs": .1, "submitMs": .1, "waitMs": .2,
                  "mapMs": .1, "compareMs": .01, "timestampReadMs": .1,
                  "polls": 2}
        rows = [{"kind": "loadedLibrary", "observed": True},
                {"kind": "adapter", "physicalSelectedVulkan": True},
                {"kind": "instrumentation", "mode": mode, "transfer": transfer,
                 "timestampsAvailable": True}]
        for index in [-2, -1, 0, 1]:
            check = index in [-2, 1]
            full = check or transfer == "verification"
            rows.append({"kind": "verification" if check else "sample",
                         "iteration": index, "ms": 1, "cpuMs": .9,
                         "exact": True, "dispatches": 4,
                         "readbackBytes": 12936 if full else 4,
                         "readbackCount": 5 if full else 1,
                         "phases": copy.deepcopy(phases) if mode != "none" else None,
                         "gpuStageNs": [100, 400, 100, 100] if mode == "gpu" else []})
        rows.append({"kind": "cleanup", "passed": True})
        return rows

    def test_all_declared_boundaries(self) -> None:
        for mode in ["none", "host", "gpu"]:
            for transfer in ["resident", "verification"]:
                with self.subTest(mode=mode, transfer=transfer):
                    self.assertEqual(summarize(self.rows(mode, transfer), 1, 1,
                                              mode, transfer, 12936)["medianMs"], 1)

    def test_dropped_work_and_invalid_timing_reject(self) -> None:
        changes = [("exact", False), ("dispatches", 3), ("readbackBytes", 0),
                   ("readbackCount", 0), ("ms", float("nan")),
                   ("cpuMs", -1), ("phases", {}), ("gpuStageNs", [1])]
        for key, value in changes:
            with self.subTest(key=key):
                rows = self.rows(); rows[5][key] = value
                with self.assertRaises(ValueError):
                    summarize(rows, 1, 1, "none", "resident", 12936)

    def test_missing_verification_settlement_or_sample_reject(self) -> None:
        for position in [0, 1, 3, 5, 6, 7]:
            with self.subTest(position=position):
                rows = self.rows(); rows.pop(position)
                with self.assertRaises(ValueError):
                    summarize(rows, 1, 1, "none", "resident", 12936)

    def test_nonpositive_gpu_interval_reject(self) -> None:
        rows = self.rows("gpu"); rows[5]["gpuStageNs"][1] = 0
        with self.assertRaises(ValueError):
            summarize(rows, 1, 1, "gpu", "resident", 12936)

    def test_phases_cannot_exceed_wall_boundary(self) -> None:
        rows = self.rows("host"); rows[5]["phases"]["waitMs"] = 2
        with self.assertRaises(ValueError):
            summarize(rows, 1, 1, "host", "resident", 12936)

    def test_report_cannot_promote_performance(self) -> None:
        report = {"schemaVersion": 1, "kind": "projection-cost-report",
                  "performanceClaim": False, "inputs": {}, "runs": [],
                  "passed": False, "failure": None}
        validate(report)
        report["performanceClaim"] = True
        from jsonschema import ValidationError
        with self.assertRaises(ValidationError):
            validate(report)


if __name__ == "__main__":
    unittest.main()
