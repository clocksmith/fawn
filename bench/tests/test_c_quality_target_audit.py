"""Focused behavioral and adversarial controls for bench/gates/c_quality_target_audit.py."""

from __future__ import annotations

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
        report = audit.run_audit(REPO_ROOT)
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
        self.assertGreater(hc["charged_production_physical_lines"], 77949)
        self.assertFalse(hc["met"])
        self.assertIn("test_span_audit_unresolved", report["unmet_claim_conditions"])
        self.assertEqual(
            report["views"]["tests_and_tooling"]["overlap_with_charged_production_paths"],
            [],
        )

        # Diagnostic CLI exits 0 when integrity passes; --require-achieved exits 1.
        self.assertEqual(audit.main(["--root", str(REPO_ROOT), "--json"]), 0)
        self.assertEqual(
            audit.main(["--root", str(REPO_ROOT), "--require-achieved"]),
            1,
        )

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
