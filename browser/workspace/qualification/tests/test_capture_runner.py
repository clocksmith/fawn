import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

from test_capture_submit import records
from test_document_submit import options, command

path = Path(__file__).parents[1] / "run-capture-attribution.py"
spec = importlib.util.spec_from_file_location("capture_runner", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CaptureRunTest(unittest.TestCase):
    def setUp(self):
        self.directory = TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.out = Path(self.directory.name)
        events, self.native, self.scope = records()
        self.native[0]["controllerRun"] = "run-a"
        self.cfg = {
            "run": "run-a",
            "scopeImage": {"sha256": options["scope_sha256"]},
            "doeLibrary": {"sha256": options["doe_sha256"]},
        }
        self.owned = {"browserPid": 8, "gpuPid": 9}
        self.outcome = {
            "diagnosticOnly": True,
            "verified": True,
            "knownEffects": 1,
            "nativeScopeRequest": self.scope,
            "imageCorrect": True,
            "committedRevision": "B-r2",
            "plannerCalls": 0,
            "modelSpendUsd": 0,
        }
        for name, value in (
            ("ready", {"run": "run-a", **self.owned}),
            ("owned", self.owned),
            ("fixture-exited", {"run": "run-a", "exitCode": 0}),
            ("bounded.trace", {"traceEvents": events}),
            ("bounded.scope", self.scope),
            ("bounded.complete", {"dataLossOccurred": False}),
            ("outcome", self.outcome),
        ):
            self.store(name, value)
        self.store_native(self.native)

    def store(self, name, value):
        (self.out / (name + ".json")).write_text(json.dumps(value))

    def store_native(self, rows):
        (self.out / "native.jsonl").write_text("".join(json.dumps(row) + "\n" for row in rows))

    def test_one_command_is_diagnostic_only(self):
        report = module.join_run(self.out, self.cfg)
        self.assertEqual(report["captureSubmission"], command)
        self.assertFalse(report["latencyQualified"])
        self.assertEqual(report["modelSpendUsd"], 0)

    def test_other_run_gpu_or_wrong_effect_refused(self):
        self.native[0]["controllerRun"] = "prior-run"
        self.store_native(self.native)
        with self.assertRaisesRegex(ValueError, "another run"):
            module.join_run(self.out, self.cfg)
        self.native[0]["controllerRun"] = self.cfg["run"]
        self.store_native(self.native)
        self.store("owned", self.owned | {"gpuPid": 123})
        with self.assertRaisesRegex(ValueError, "lifetimes"):
            module.join_run(self.out, self.cfg)
        self.store("owned", self.owned)
        for wrong in (
            {"knownEffects": 2},
            {"committedRevision": "A-r2"},
            {"imageCorrect": False},
        ):
            with self.subTest(wrong=wrong):
                self.store("outcome", self.outcome | wrong)
                with self.assertRaisesRegex(ValueError, "verified effect"):
                    module.join_run(self.out, self.cfg)

    def test_lookalike_must_have_executed_when_requested(self):
        with self.assertRaisesRegex(ValueError, "fault control"):
            module.join_run(self.out, self.cfg | {"lookalike": True})
        control = {"otherTrackLive": True, "copiedLabelSubmitted": True, "privateCaptureVisible": False}
        self.store("outcome", self.outcome | {"mainWorldControl": control})
        self.assertEqual(
            module.join_run(self.out, self.cfg | {"lookalike": True})["mainWorldControl"], control
        )

    def test_named_scope_alone_does_not_supply_a_fence(self):
        truncated = copy.deepcopy(self.native)
        truncated.pop(2)
        self.store_native(truncated)
        with self.assertRaises(ValueError):
            module.join_run(self.out, self.cfg)

    @unittest.skipUnless(sys.platform == "linux", "Linux child ownership")
    def test_cdp_cannot_select_an_existing_process(self):
        ready = {
            "run": self.cfg["run"],
            "controllerPid": os.getpid(),
            "browserPid": os.getppid(),
            "gpuPid": 999999,
        }
        with self.assertRaisesRegex(ValueError, "outside the fixture"):
            module.check_ready(ready, os.getpid(), self.cfg)

    @unittest.skipUnless(sys.platform == "linux", "Linux private descendant cleanup")
    def test_cleanup_owns_detached_descendant_only(self):
        # The unit test process is a sibling of the controller, with one
        # unrelated sentinel. A dedicated child invokes the actual cleanup.
        sentinel = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(15)"])
        try:
            code = """import ctypes, os, runpy, subprocess, sys
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import patch
m = runpy.run_path(sys.argv[1])
assert ctypes.CDLL(None).prctl(36,1,0,0,0) == 0
worker = subprocess.Popen([sys.executable, "-c", "import os,sys,time; p=os.fork(); print(p,flush=True) if p else (os.setsid(),time.sleep(12)); os._exit(0)"], stdout=subprocess.PIPE, text=True)
pid = int(worker.stdout.readline())
worker.wait()
assert m["belongs"](pid, os.getpid())
force_stat = sys.argv[2] == "parent-stat"
with patch.object(Path, "is_file", return_value=False) if force_stat else nullcontext():
    m["cleanup_owned"]()
assert m["proc"](pid) is None
"""
            for mode in ("kernel", "parent-stat"):
                with self.subTest(mode=mode):
                    subprocess.run([sys.executable, "-c", code, str(path), mode], check=True, timeout=9)
                    self.assertIsNone(sentinel.poll())
        finally:
            sentinel.terminate()
            sentinel.wait(timeout=3)
