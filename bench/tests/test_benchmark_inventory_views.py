"""Lossless storage and generated navigation regression controls."""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

import jsonschema

from bench.inventory import dashboard, markdown, model, storage
from bench.tests.test_benchmark_inventory import observation, ROOT


def fixture() -> dict:
    old = observation("old", "2026-10-01T00:00:00Z", qualified=True)
    new = observation("new", "2026-11-01T00:00:00Z", commit="b" * 40)
    new["metrics"][0].update(
        implementationValue=3, comparatorValue=2, ratio=1.5, outcome="loss"
    )
    execution = observation("process", None, machine=None)
    execution["observationLevel"] = "execution"
    execution["executionStatus"] = "failed"
    # Re-finalize after the deliberate fixture level change.
    model.finalize_inventory_row(execution)
    rows = [old, new, execution]
    for row in rows:
        row["familyId"] = "fixture"
    return {
        "schemaVersion": 2,
        "kind": "benchmark-cube-inventory",
        "currentSource": "c" * 40,
        "policy": {"path": "policy.json", "sha256": "a" * 64},
        "sources": {"path": "sources.json", "sha256": "b" * 64},
        "families": [
            {
                "id": "fixture",
                "label": "Fixture family",
                "section": "application",
                "expectedOs": ["linux"],
                "observations": [r["rowId"] for r in rows],
            }
        ],
        "rows": rows,
        "current": model.select_inventory_current(rows, "c" * 40),
    }


class TestInventoryStorage(unittest.TestCase):
    def test_round_trip_preserves_missing_null_zero_bool_and_samples(
        self,
    ) -> None:
        value = fixture()
        metric = value["rows"][0]["metrics"][0]
        metric.update(
            implementationValue=-0.0,
            comparatorValue=False,
            ratio=None,
            samples={
                "implementation": 0,
                "comparator": None,
                "unit": "observations",
            },
        )
        del value["rows"][1]["metrics"][0]["ratio"]
        before = storage.canonical(value)
        encoded = storage.compact(value)
        self.assertEqual(storage.canonical(storage.expand(encoded)), before)
        self.assertEqual(storage.compact(value), encoded)
        self.assertEqual(storage.expand(value), value)
        encoded["rows"][0]["machineRef"] = -1
        with self.assertRaisesRegex(ValueError, "reference"):
            storage.expand(encoded)

    def test_interning_never_merges_identity_or_shares_mutable_expansion(
        self,
    ) -> None:
        value = fixture()
        encoded = storage.compact(value)
        self.assertEqual(len(encoded["rows"]), 3)
        self.assertEqual(
            encoded["rows"][0]["machineRef"], encoded["rows"][1]["machineRef"]
        )
        expanded = storage.expand(encoded)
        expanded["rows"][0]["machine"]["id"] = "changed"
        self.assertEqual(expanded["rows"][1]["machine"]["id"], "host-a")
        self.assertEqual(value["rows"][0]["machine"]["id"], "host-a")
        self.assertEqual(expanded["current"], value["current"])

    def test_unknown_and_wrong_schema_versions_fail_explicitly(self) -> None:
        with self.assertRaises(ValueError):
            storage.expand({"schemaVersion": 9})
        schema = json.loads(
            (ROOT / "config/benchmark-cube-row.schema.json").read_text()
        )
        row = storage.compact(fixture())["rows"][0]
        jsonschema.validate(row, schema)
        row["machineRef"] = True
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate(row, schema)


class TestInventoryViews(unittest.TestCase):
    def test_unbound_history_never_becomes_one_hosts_current_result(self) -> None:
        value = fixture()
        for row in value["rows"]:
            row["machine"]["id"] = "unknown:" + row["registrationId"]
        front = markdown.render_current(value)
        self.assertEqual(front.count("[Fixture family]"), 1)
        self.assertIn("unbound history (not one host)", front)
        self.assertIn("host-specific current result unknown", front)
        self.assertNotIn("latest qualified: workload", front)
        self.assertIn("1/1/0/0", front)

    def test_family_without_observations_remains_discoverable(self) -> None:
        value = fixture()
        value["rows"] = []
        value["current"] = []
        front = markdown.render_current(value)
        self.assertIn("[Fixture family]", front)
        self.assertIn("no registered observations", front)

    def test_overview_keeps_actual_machines_separate(self) -> None:
        value = fixture()
        second = deepcopy(value["rows"][1])
        second.update(rowId="second-host", registrationId="second-host")
        second["machine"]["id"] = "host-b"
        model.finalize_inventory_row(second)
        value["rows"].append(second)
        value["current"] = model.select_inventory_current(value["rows"], value["currentSource"])
        front = markdown.render_current(value)
        self.assertEqual(front.count("[Fixture family]"), 2)
        self.assertIn("host-a / linux", front)
        self.assertIn("host-b / linux", front)

    def test_late_contract_date_does_not_make_undated_history_current(self) -> None:
        value = fixture()
        value["rows"][0]["executedAt"] = None
        value["rows"][0]["contractFrozenAt"] = "2099-01-01T00:00:00Z"
        value["current"] = model.select_inventory_current(value["rows"], value["currentSource"])
        front = markdown.render_current(value)
        self.assertIn("2026-11-01T00:00:00Z", front)
        self.assertNotIn("2099", front)
        self.assertIn("0/1/0/0", front)

    def test_front_page_links_history_without_hiding_older_qualified_result(
        self,
    ) -> None:
        value = fixture()
        front = markdown.render_current(value)
        self.assertIn("(families/fixture.md)", front)
        self.assertNotIn(value["rows"][0]["rowId"], front)
        self.assertIn("current source unmeasured", front)
        self.assertIn("0/1/0/0", front)
        self.assertIn("latest qualified: workload at 2026-10-01", front)
        self.assertIn("history: 1 other failed records", front)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pages = markdown.render_families(value, root / "CURRENT.md", root)
            history = pages[root / "families/fixture.md"]
            for row in value["rows"][:2]:
                self.assertIn(row["rowId"], history)
            self.assertIn("qualified", history)
            self.assertIn("loss", history)
            self.assertNotIn(value["rows"][2]["rowId"], history)

    def test_loss_only_classification_cannot_be_presented_as_a_win(
        self,
    ) -> None:
        value = fixture()
        for row in value["rows"]:
            row["metrics"][0]["outcome"] = "not-classified-as-loss"
        counts = markdown.outcome_counts(value["rows"])
        self.assertEqual(counts["win"], 0)
        self.assertEqual(counts["inconclusive"], 0)
        self.assertEqual(counts["unknown"], 2)

    def test_dashboard_is_self_contained_and_escapes_receipt_script_content(
        self,
    ) -> None:
        value = fixture()
        value["rows"][0]["workloadId"] = "</script><script>bad()</script>"
        html = dashboard.render_dashboard(
            storage.compact(value), ROOT / "bench/out/view.html"
        )
        self.assertNotIn("</script><script>bad()", html)
        self.assertIn(
            "IBM Plex", html
        )  # Existing theme is exercised, not copied.
        self.assertIn("Summary records", html)
        self.assertIn("Individual executions", html)


def write_browser_fixture(path: Path) -> None:
    value = fixture()
    value["rows"][0]["workloadId"] = "historic-render"
    value["rows"][1]["workloadId"] = "new-render"
    value["rows"][2]["workloadId"] = "failed-process"
    for index in range(101):
        row = deepcopy(value["rows"][0])
        row["rowId"] = f"pagination-{index}"
        row["workloadId"] = f"pagination-{index}"
        value["rows"].append(row)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dashboard.render_dashboard(value, path), encoding="utf-8")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Emit a synthetic dashboard UI fixture"
    )
    parser.add_argument("--dashboard-fixture", type=Path, required=True)
    write_browser_fixture(parser.parse_args().dashboard_fixture)
