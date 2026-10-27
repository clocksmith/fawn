"""Reject package timing comparisons with missing work or misattributed providers."""

from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import unittest

from bench.tools.compare_c_vs_zig_package import cases, environment, pair_failures, sample_failures
from bench.tools.prepare_package_oracle_plans import prepare


LIBRARY = Path("/tmp/test-provider/libwebgpu_doe_c.dylib")


def valid_meta() -> dict:
    return {
        "packageProviderIdentity": {"loaded": True, "doeNative": True, "libraryFlavor": "doe-c",
                                    "doeLibraryPath": str(LIBRARY)},
        "executionErrorCount": 0, "executionUnsupportedCount": 0, "executionSkippedCount": 0,
        "executionSuccessCount": 3, "timingClass": "operation", "timingMs": 1,
        "packagePreparedSession": False, "packageSetupIncludedInSelectedTiming": True,
        "readbackCaptures": [{"byteLength": 4, "sha256": "1" * 64}],
        "packageEffectiveReadbackPaths": ["mapAsync"], "packageReadbackActualPaths": ["mapped-native-read-copy"],
        "adapterInfo": {"device": "test physical GPU", "isFallbackAdapter": False},
        "planHash": "same-plan", "executionShape": {"dispatchCount": 1},
        "executionDispatchCount": 1, "executionSubmitCount": 1,
        "queueWaitMode": "readback-or-fence.mapAsync",
    }


class PackageComparisonTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX signal/session control")
    def test_parent_sigterm_reaps_current_worker_and_restores_handlers(self) -> None:
        script = """
import json,signal,sys,tempfile
from pathlib import Path
from bench.tools.compare_c_vs_zig_compilation import process_sample
from bench.tools.compare_c_vs_zig_package import OwnedWorkers
report={};old=signal.getsignal(signal.SIGTERM)
with tempfile.TemporaryDirectory() as out:
 with OwnedWorkers(report) as workers:
  def started(child):
   workers.started(child)
   print(child.pid,flush=True)
  sample=process_sample([sys.executable,'-c','import time;time.sleep(60)','doe-owned-cancel-fixture'],Path(out)/'sample',10,child_started=started)
  workers.child=None
  signals=list(workers.signals)
print(json.dumps({'signals':signals,'exitCode':sample['exitCode'],'unknown':report.get('cancelledGpuCompletionUnknown'),'restored':signal.getsignal(signal.SIGTERM)==old}),flush=True)
"""
        worker_pid = None
        child = subprocess.Popen([sys.executable, "-c", script], cwd=Path(__file__).resolve().parents[2],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            ready, _, _ = select.select([child.stdout], [], [], 5)
            self.assertTrue(ready, "fixture did not publish its owned worker")
            worker_pid = int(child.stdout.readline())
            os.kill(child.pid, signal.SIGTERM)
            stdout, stderr = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 0, stderr)
            result = json.loads(stdout)
            self.assertEqual(result["signals"], [signal.SIGTERM])
            self.assertLess(result["exitCode"], 0)
            self.assertTrue(result["unknown"])
            self.assertTrue(result["restored"])
            with self.assertRaises(ProcessLookupError):
                os.kill(worker_pid, 0)
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
                child.communicate(timeout=5)
            if worker_pid is not None:
                observed = subprocess.run(["ps", "-p", str(worker_pid), "-o", "args="],
                                          capture_output=True, text=True, timeout=5)
                if "doe-owned-cancel-fixture" in observed.stdout:
                    os.killpg(worker_pid, signal.SIGKILL)

    def test_missing_work_or_provider_evidence_cannot_pass(self) -> None:
        good = valid_meta()
        self.assertEqual(sample_failures(good, LIBRARY, "c", False, 4, "1" * 64), [])
        for field in ("packageProviderIdentity", "executionErrorCount", "executionUnsupportedCount",
                      "executionSkippedCount", "executionSuccessCount", "timingClass", "timingMs",
                      "packagePreparedSession", "readbackCaptures", "packageEffectiveReadbackPaths", "adapterInfo"):
            bad = copy.deepcopy(good)
            del bad[field]
            self.assertTrue(sample_failures(bad, LIBRARY, "c", False, 4, "1" * 64), field)
        for field, value in (("doeLibraryPath", "/tmp/another-provider"), ("loaded", False),
                             ("libraryFlavor", "doe-dropin")):
            bad = copy.deepcopy(good)
            bad["packageProviderIdentity"][field] = value
            self.assertTrue(sample_failures(bad, LIBRARY, "c", False, 4, "1" * 64))

    def test_wrong_capture_or_scope_and_nonphysical_adapter_reject(self) -> None:
        for mutation in (
            {"executionErrorCount": 1}, {"executionSkippedCount": 1},
            {"readbackCaptures": [{"byteLength": 3, "sha256": "1" * 64}]},
            {"readbackCaptures": [{"byteLength": 4, "sha256": "2" * 64}]},
            {"packagePreparedSession": True}, {"timingMs": 0},
            {"timingMs": float("nan")}, {"timingMs": float("inf")},
            {"adapterInfo": {"device": "CPU", "isFallbackAdapter": True}},
        ):
            self.assertTrue(sample_failures({**valid_meta(), **mutation}, LIBRARY, "c", False, 4, "1" * 64))

    def test_matching_counters_do_not_hide_different_actual_readback(self) -> None:
        good = valid_meta()
        self.assertEqual(pair_failures(good, copy.deepcopy(good)), [])
        for field, value in (("packageReadbackActualPaths", ["map-read-copy-unmap"]),
                             ("executionShape", {"dispatchCount": 0}), ("executionSubmitCount", 0),
                             ("planHash", "different"), ("packageSetupIncludedInSelectedTiming", False)):
            self.assertTrue(pair_failures(good, {**good, field: value}), field)

    def test_child_environment_restores_even_after_exception(self) -> None:
        before = os.environ.get("DOE_WEBGPU_LIB")
        with self.assertRaises(RuntimeError):
            with environment({"DOE_WEBGPU_LIB": "test-only-selected-provider"}):
                self.assertEqual(os.environ["DOE_WEBGPU_LIB"], "test-only-selected-provider")
                raise RuntimeError("control")
        self.assertEqual(os.environ.get("DOE_WEBGPU_LIB"), before)

    def test_catalog_cases_keep_prepared_and_fullspan_contracts(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "oracles"
            prepare(root)
            rows = cases(root, ["node-package", "node-direct", "bun-full",
                                "node-package-prepared", "node-direct-prepared", "bun-full-prepared"])
            self.assertEqual(len(rows), 27)
            self.assertEqual(sum(row["prepared"] for row in rows), 12)
            vector = [row for row in rows if row["id"] == "package_vector_scale_add_262k"]
            self.assertEqual(len(vector), 6)
            self.assertTrue(all(row["fullOutputBytes"] == 1048576 for row in vector))
            manifest = root / "oracle-manifest.json"
            data = json.loads(manifest.read_text())
            data["plans"][0]["planSha256"] = "0" * 64
            manifest.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                cases(root, ["node-package"])


if __name__ == "__main__":
    unittest.main()
