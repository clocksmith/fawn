"""Controls for policy boundaries and reproducible aggregate classification."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from bench.tools.native_catalog_report import check_retained, classify_catalogs, is_loss, main


class TestNativeCatalogReport(unittest.TestCase):
    def setUp(self) -> None:
        self.policy = {
            "schemaVersion": 1, "id": "fixture-policy", "comparison": "doe/dawn",
            "ratioExclusiveMinimum": 1.2,
            "confidenceLowerExclusiveMinimum": 1.01,
            "doeMedianMinimum": {"milliseconds": 0.25, "max_rss_mib": 2},
            "operationMetrics": ["cold.operation_ms", "warm.operation_ms"],
            "headlineMetric": "warm.operation_ms",
        }

    def test_strict_ratio_and_confidence_with_inclusive_absolute_floor(self) -> None:
        comparison = {"p50": 1.3, "ci95": [1.1, 1.5]}
        self.assertTrue(is_loss("warm.operation_ms", comparison,
                                {"p50": 0.25}, self.policy))
        for changed in ({"p50": 1.2}, {"ci95": [1.01, 1.5]}):
            self.assertFalse(is_loss("warm.operation_ms",
                                     comparison | changed,
                                     {"p50": 0.5}, self.policy))
        self.assertFalse(is_loss("warm.operation_ms", comparison,
                                 {"p50": 0.249}, self.policy))
        self.assertFalse(is_loss("max_rss_mib", comparison,
                                 {"p50": 1}, self.policy))
        self.assertTrue(is_loss("max_rss_mib", comparison,
                                {"p50": 2}, self.policy))

    def test_nonfinite_and_boolean_statistics_reject(self) -> None:
        for invalid in (float("nan"), float("inf"), True):
            with self.assertRaisesRegex(ValueError, "finite number"):
                is_loss("cpu_ms", {"p50": invalid, "ci95": [1.1, 1.5]},
                        {"p50": 1}, self.policy)

    def test_counts_ranks_and_text_share_classification_and_check_is_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            policy_path = root / "policy.json"
            policy_path.write_text(json.dumps(self.policy), encoding="utf-8")
            catalog = {
                "backend": "fixture-backend", "contract": {"gitCommit": "frozen"},
                "supportedCount": 1, "unsupportedCount": 1,
                "workloads": [
                    {"id": "live", "supported": True, "pairs": 3,
                     "arms": {
                         "doe": {"warm.operation_ms": {"p50": 3, "p95": 4},
                                 "cold.operation_ms": {"p50": 1, "p95": 2}},
                         "dawn": {"warm.operation_ms": {"p50": 1, "p95": 2},
                                  "cold.operation_ms": {"p50": 1, "p95": 2}},
                     },
                     "comparisons": {
                         "doe/dawn": {
                             "warm.operation_ms": {"p50": 3, "p95": 2, "ci95": [2, 4]},
                             "cold.operation_ms": {"p50": 1, "p95": 1, "ci95": [0.9, 1.1]},
                         },
                         "dawn-aa/dawn": {"warm.operation_ms": {"p50": 1}},
                     }},
                    {"id": "unsupported", "supported": False},
                ],
            }
            catalog_path = root / "catalog_fixture.json"
            catalog_bytes = json.dumps(catalog).encode()
            catalog_path.write_bytes(catalog_bytes)
            report = classify_catalogs(root, policy_path)
            self.assertEqual(report["totalPhaseLosses"], 1)
            self.assertEqual(report["totalOperationLosses"], 1)
            self.assertEqual(report["headlineLosses"], 1)
            self.assertEqual(report["allPhaseLosses"][0]["phase"], "warm.operation_ms")
            self.assertEqual(report["backends"][0]["pairCounts"], {3: 1})
            retained = root / "retained.json"
            retained.write_text(json.dumps(report), encoding="utf-8")
            check_retained(report, retained)
            changed = json.loads(retained.read_text(encoding="utf-8"))
            changed["allPhaseLosses"] = []
            retained.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing"):
                check_retained(report, retained)
            args = ["--catalog", str(root), "--policy", str(policy_path),
                    "--out", str(root / "out")]
            self.assertEqual(main(args), 0)
            self.assertEqual(main(args + ["--check"]), 0)
            self.assertEqual(catalog_path.read_bytes(), catalog_bytes)
            text = root / "out/classification.md"
            text.write_text("stale", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "stale"):
                main(args + ["--check"])
            self.assertEqual(text.read_text(encoding="utf-8"), "stale")


if __name__ == "__main__":
    unittest.main()
