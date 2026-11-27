"""Focused behavioral and adversarial controls for bench/gates/c_quality_target_audit.py."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from bench.gates import c_quality_target_audit as audit  # noqa: E402
from bench.tests.historical_sources import historical_files  # noqa: E402


class CQualityTargetAuditTest(unittest.TestCase):
    def test_physical_line_algorithm_exact_bytes(self) -> None:
        self.assertEqual(audit.count_physical_lines(b""), 0)
        self.assertEqual(audit.count_physical_lines(b"one\n"), 1)
        self.assertEqual(audit.count_physical_lines(b"one"), 1)
        self.assertEqual(audit.count_physical_lines(b"one\n\ntwo"), 3)
        self.assertEqual(audit.count_physical_lines(b"one\r\ntwo\n"), 2)
        self.assertEqual(audit.count_physical_lines(b"\xff\xfe\nraw"), 2)

    def test_test_span_half_open_byte_reconciliation(self) -> None:
        source = b"int prod(void) { return 1; }\n/* test */\nvoid t(void) {}\n"
        intervals = audit.physical_line_byte_intervals(source)
        self.assertEqual(len(intervals), 3)
        test_start = intervals[1][0]
        test_end = intervals[2][1]

        impl, excl, errors = audit.evaluate_file_test_spans(
            source,
            [
                {
                    "start_byte": test_start,
                    "end_byte": test_end,
                    "purpose": "exclusively_test_only",
                    "suite": "unit",
                    "reviewed": True,
                }
            ],
        )
        self.assertEqual(errors, [])
        self.assertEqual(impl, 1)
        self.assertEqual(excl, 2)
        self.assertEqual(impl + excl, audit.count_physical_lines(source))

        # Partial/mixed line span must NOT remove the shared line from implementation.
        impl_mixed, excl_mixed, errors_mixed = audit.evaluate_file_test_spans(
            source,
            [
                {
                    "start_byte": intervals[0][0] + 4,
                    "end_byte": intervals[1][1],
                    "purpose": "exclusively_test_only",
                    "suite": "unit",
                    "reviewed": True,
                }
            ],
        )
        self.assertEqual(errors_mixed, [])
        self.assertEqual(excl_mixed, 1)
        self.assertEqual(impl_mixed, 2)

        # Unreviewed or out-of-bounds spans fail closed.
        _, _, bad_errors = audit.evaluate_file_test_spans(
            source,
            [
                {
                    "start_byte": 0,
                    "end_byte": len(source) + 1,
                    "purpose": "exclusively_test_only",
                    "suite": "unit",
                    "reviewed": True,
                },
                {
                    "start_byte": 0,
                    "end_byte": 5,
                    "purpose": "exclusively_test_only",
                    "suite": "unit",
                    "reviewed": False,
                },
            ],
        )
        self.assertEqual(len(bad_errors), 2)

    def test_live_repository_audit_integrity_and_honest_unachieved_state(self) -> None:
        # The current source closure can always be counted. Its implementation
        # exclusions require a separate review bound to these exact source bytes.
        report = audit.run_audit(REPO_ROOT, test_spans_path=Path("__NONE__"))
        self.assertTrue(report["integrity_ok"], msg=report.get("integrity_errors"))
        self.assertFalse(report["achieved"])
        self.assertEqual(
            report["baseline"]["inventory_sha256"],
            "9bb2524f32d724a0d74b9a3a76c28c39719aef59bd5cb6b089fa6dc87bbd613d",
        )
        self.assertEqual(report["baseline"]["inventory_files"], 809)
        self.assertEqual(report["baseline"]["inventory_physical_lines"], 222409)
        self.assertEqual(report["baseline"]["headline_files"], 628)
        self.assertEqual(report["baseline"]["headline_physical_lines"], 155898)
        self.assertEqual(report["baseline"]["production_lexical_test_declarations"], 665)

        hc = report["headline_check"]
        self.assertEqual(hc["fixed_headline_ceiling_lines"], 77949)
        self.assertEqual(hc["met"], hc["charged_production_physical_lines"] <= 77949)

        impl_view = report["views"]["baseline_and_final_implementation_only"]
        self.assertEqual(impl_view["status"], "unresolved")
        self.assertIsNone(impl_view["candidate_excluded_test_only_lines"])
        self.assertFalse(impl_view["normalized_condition_met"])

        self.assertEqual(
            report["views"]["transitional_c_zig_adapters"][
                "zig_references_in_charged_production"
            ],
            [],
        )
        self.assertIn("test_span_audit_unresolved", report["unmet_claim_conditions"])
        self.assertNotIn(
            "charged_production_references_zig",
            report["unmet_claim_conditions"],
        )
        self.assertEqual(
            report["views"]["tests_and_tooling"]["overlap_with_charged_production_paths"],
            [],
        )

        args = ["--root", str(REPO_ROOT), "--no-test-spans"]
        self.assertEqual(audit.main(args), 0)
        self.assertEqual(audit.main([*args, "--require-achieved"]), 1)

    def test_retained_span_audit_matches_its_reviewed_source(self) -> None:
        revision = "c6b05a0f328ebab29f23b1f7e4141d0875ca5df0"
        path = "docs/c-migration/test-span-audit.json"
        _, receipt_bytes = next(historical_files(REPO_ROOT, revision, [path]))
        receipt = json.loads(receipt_bytes)
        self.assertEqual(receipt["baseline_summary"]["implementation_only_lines"], 143407)
        self.assertEqual(receipt["baseline_summary"]["excluded_test_only_lines"], 12491)
        records = receipt["candidate_files"]
        self.assertEqual(len(records), 987)
        # One Git read for the reviewed closure. The working tree may have advanced.
        excluded = implemented = 0
        for name, data in historical_files(REPO_ROOT, revision, records):
            record = records[name]
            self.assertEqual(hashlib.sha256(data).hexdigest(), record["sha256"], name)
            lines, skipped, errors = audit.evaluate_file_test_spans(data, record["spans"])
            self.assertEqual(errors, [], name)
            self.assertEqual((lines, skipped),
                             (record["implementation_lines"], record["excluded_test_only_lines"]), name)
            implemented += lines
            excluded += skipped
        self.assertEqual((implemented, excluded), (171655, 24))

    def test_default_span_evidence_requires_the_live_closure(self) -> None:
        span_file = REPO_ROOT / "docs/c-migration/test-span-audit.json"
        receipt = json.loads(span_file.read_text(encoding="utf-8"))
        records = receipt["candidate_files"]
        declared = hashlib.sha256()
        for path, record in sorted(records.items()):
            declared.update(path.encode() + b"\0" + record["sha256"].encode() + b"\n")
        report = audit.run_audit(REPO_ROOT)
        observed = report["report_identity"]["source_closure_hashes"]["charged_production_closure_sha256"]
        view = report["views"]["baseline_and_final_implementation_only"]
        if declared.hexdigest() == observed:
            self.assertTrue(report["integrity_ok"], report["integrity_errors"])
            self.assertEqual(view["status"], "resolved")
        else:
            self.assertFalse(report["integrity_ok"])
            self.assertFalse(report["achieved"])
            self.assertEqual(view["status"], "unresolved")
            self.assertIsNone(view["candidate_implementation_only_lines"])
            self.assertIn("test_span_audit_unresolved", report["unmet_claim_conditions"])

        # Removing a production file also invalidates the reviewed closure.
        # Use a leading name so this injected failure is visible in the bounded report.
        removed = "000-retired-source.c"
        records[removed] = next(iter(records.values()))
        with tempfile.TemporaryDirectory() as directory:
            modified = Path(directory) / "spans.json"
            modified.write_text(json.dumps(receipt), encoding="utf-8")
            rejected = audit.run_audit(REPO_ROOT, test_spans_path=modified)
        self.assertIn(f"candidate span record outside charged closure: {removed}",
                      rejected["integrity_errors"])
        self.assertFalse(rejected["achieved"])

    def test_tampered_baseline_hash_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)

            (root / "docs" / "c-migration").mkdir(parents=True)
            contract = json.loads(
                (REPO_ROOT / "docs/c-migration/quality-counting-contract.json").read_text(
                    encoding="utf-8"
                )
            )
            contract["baseline"]["inventory_sha256"] = "0" * 64
            (root / "docs/c-migration/quality-counting-contract.json").write_text(
                json.dumps(contract), encoding="utf-8"
            )
            (root / "docs/c-migration/zig-inventory.json").write_bytes(
                (REPO_ROOT / "docs/c-migration/zig-inventory.json").read_bytes()
            )
            (root / "runtime/c/src").mkdir(parents=True)
            (root / "runtime/c/src/unit.c").write_bytes(b"int x(void) { return 0; }\n")
            for rel in audit.PINNED_DEPENDENCY_PATHS:
                dest = root / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(b"/* pinned */\n")

            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "init"], cwd=root, check=True)

            report = audit.run_audit(root)
            self.assertFalse(report["integrity_ok"])
            self.assertFalse(report["achieved"])
            self.assertTrue(
                any("sha256 mismatch" in err for err in report["integrity_errors"])
            )
            self.assertEqual(audit.main(["--root", str(root)]), 1)


if __name__ == "__main__":
    unittest.main()
