"""Trace coordinates cannot establish an operation duration in either benchmark runner."""

import json
import runpy
import sys
import tempfile
import unittest
from pathlib import Path

BENCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCH))

from native_compare_modules.timing_selection import (
    classify_timing_source, maybe_adjust_timing_for_ignored_first_ops, pick_measured_timing_ms,
)

single_runtime_timing = runpy.run_path(str(BENCH / "single-runtime/run_bench.py"))["pick_measured_timing_ms"]


class TraceWindowTests(unittest.TestCase):
    def test_both_consumers_reject_sequence_or_unusable_windows(self):
        def row(timestamp, source="execution-monotonic"):
            return {"timestampMonoNs": timestamp, "timestampSource": source, "executionDurationNs": 0}

        cases = [
            [row(1), row(2_000_001)],
            [row(1, "sequence"), row(2, "sequence")],
            [{"seq": index, "timestampMonoNs": index} for index in range(3)],
            [row(1) | {"runId": "first"}, row(2_000_001) | {"runId": "second"}],
            [row(1) | {"scope": "build"}, row(2_000_001) | {"scope": "dispatch"}],
            [row(1) | {"clockDomain": "device"}, row(2_000_001) | {"clockDomain": "host"}],
        ]
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.jsonl"
            for rows in cases:
                with self.subTest(rows=rows):
                    trace.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
                    compare = pick_measured_timing_ms(125.0, {}, trace, "operation", None)
                    cached = pick_measured_timing_ms(125.0, {"timingMs": 1.0, "timingSource": "doe-trace-window"},
                                                     trace, "operation", None)
                    single = single_runtime_timing(125.0, rows, {})
                    for elapsed, source, _ in (compare, cached, single):
                        self.assertEqual((elapsed, source), (125.0, "wall-time"))

    def test_legacy_row_delta_is_ineligible_for_operation_claims(self):
        self.assertEqual(classify_timing_source("doe-trace-window"), "unknown")

    def test_untimed_rows_do_not_overwrite_wall_time_after_warmup(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.jsonl"
            trace.write_text('{"executionDurationNs": 100}\n{"executionDurationNs": 0}\n', encoding="utf-8")
            elapsed, source, meta = maybe_adjust_timing_for_ignored_first_ops(
                measured_ms=10.0, measured_source="wall-time", trace_jsonl=trace, ignore_first_ops=1)
            self.assertEqual((elapsed, source), (10.0, "wall-time"))
            self.assertFalse(meta["uploadIgnoreFirstApplied"])
