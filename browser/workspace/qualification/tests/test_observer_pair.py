import json
from pathlib import Path
import runpy
import tempfile
import unittest

QUAL = Path(__file__).resolve().parents[1]
runner = runpy.run_path(str(QUAL / "resident-comparison.py"))
wire = runpy.run_path(str(Path(__file__).with_name("test_wire_submit.py")))
pin = json.loads((QUAL / "resident-command-witness-pin.json").read_text())


def record(path, name, obj):
    runner["write"](path / (name + ".json"), obj)


def make_row(path, wave, pid):
    events, scope, command, (_, gpu, *_) = wire["fixture"]()
    proof = wire["wire"]["wire_submit"](events, scope, 9, 14, command)
    on = wave["observer"] == "on"
    arm = wave["arm"]
    source = dict(chromeSha256="9" * 64, chromium="a" * 40, dawn="b" * 40, hostBootSha256="c" * 64)
    if arm.startswith("fawn"):
        pid = 9  # The test trace has browser 9, renderer 10 and GPU 14.
        select = dict(executable={"sha256": source["chromeSha256"]},
                      runtime={"mode": "doe-c" if arm == "fawn-doe" else "dawn", "library": {"sha256": pin["doeSha256"]}})
    else:
        select = {"channel": "chromium"}
    surface = scope["captureId"]
    observer = dict(mode=wave["observer"], browserPid=pid, targetId="page47", captureId=surface,
                    category="disabled-by-default-gpu.dawn" if on else None)
    progress = dict(phase="reconciled", browserPid=pid, gpuProcessPid=14, selection=select, targetId="page47",
                    surfaceId=surface, nodeTimeOriginMs=2000, browser={"product": "Chromium/145"},
                    gpu={"vendor": "intel", "isFallbackAdapter": False})
    progress["observedRuntime"] = dict(gpuProcessPid=14, sandboxed=True,
        selectors=["--use-webgpu-runtime=" + arm.split("-")[-1]] if arm.startswith("fawn") else [],
        frameObserver=on and arm.startswith("fawn"))
    outcome = dict(verified=True, imageCorrect=True, knownEffects=1, committedRevision="B-r2", selection=select,
                   requestMonotonicMs=2, mutationReturnedMonotonicMs=3, outcomeVerifiedMonotonicMs=13 if on else 10,
                   clock="request-repaint-through-authoritative-reconciliation", observer=observer,
                   plannerCalls=0, modelSpendUsd=0, digestMode="cpu")
    if on and arm.startswith("fawn"):
        progress["frameImportScope"] = {k: v for k, v in scope.items() if k not in ("ordinal", "width", "height", "mediaTimestampUs")}
        outcome.update(diagnosticOnly=True, diagnosticKind="frame-import", frameImportOrdinal=scope["ordinal"],
                       frameImportTimestampUs=scope["mediaTimestampUs"], frameImportDimensions=[scope["width"], scope["height"]])
        if arm == "fawn-doe":
            source["doeMapping"] = dict(gpuPid=14, sha256=pin["doeSha256"], buildId=pin["doeBuildId"],
                                        compilerSha256=pin["compilerSha256"], executableMappingVerified=True)
            events.append(dict(gpu, name="FawnResidentCommandTerminal", ts=3.4, args=dict(
                binding=proof["documentBinding"], submission=proof["submission"], state=3, serial="3",
                callbackStatus=1, callbackSuccess=True, fromSelectedImage=True, buildId=pin["doeBuildId"])))
        record(path, "frame-import.scope", scope)
        record(path, "frame-import.trace", {"traceEvents": events})
        record(path, "frame-import.complete", {"dataLossOccurred": False})
    elif on:
        record(path, "stock-observer.scope", observer)
        record(path, "stock-observer.trace", {"traceEvents": []})
        record(path, "stock-observer.complete", {"dataLossOccurred": False})
    record(path, "progress", progress)
    record(path, "outcome", outcome)
    record(path, "browser-source", source)
    record(path, "exit", {"exit": 0, "allReady": True, "waveTimeout": False,
                          "gpuProcessDrm": {"devicesSeen": ["amdgpu@0000:03:00.0"]}})


class ObserverPairTest(unittest.TestCase):
    def test_alternates_and_qualifies_only_same_mode_pairs(self):
        plan = runner["make_plan"](2, [1], True)
        helper = [w["observer"] for w in plan if w["arm"] == "stock-helper"]
        self.assertEqual(helper, ["on", "off", "off", "on"])
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            record(root, "plan", plan)
            for index, wave in enumerate(plan):
                row = root / runner["run_id"](wave, 1)
                row.mkdir()
                make_row(row, wave, 50 + index)
            report = runner["summarize"](root)
            for group in report["traceOn"]["paired"]:
                self.assertEqual(group["n"], 2)
            for group in report["observerEffect"]:
                if group["arm"] == "fawn-doe":
                    self.assertEqual(group["n"], 0)
                else:
                    self.assertEqual(group["p50BoundsMs"], [2, 4])
            # A positive receipt on an adjacent row never qualifies this timed attempt.
            wave = next(w for w in plan if w["arm"] == "fawn-doe" and w["observer"] == "on")
            row = root / runner["run_id"](wave, 1)
            trace = runner["read"](row / "frame-import.trace.json")
            trace["traceEvents"] = [e for e in trace["traceEvents"] if e["name"] != "FawnResidentCommandTerminal"]
            record(row, "frame-import.trace", trace)
            refused = runner["summarize_row"](row, wave, 1)
            self.assertEqual(refused["status"], "unqualified")
            self.assertIsNone(refused["requestMonotonicMs"])
            pair = runner["summarize"](root)["traceOn"]["paired"][-1]
            self.assertEqual(pair["n"], 1)

    def test_same_image_can_use_another_card_or_host(self):
        source = dict(chromeSha256="9" * 64, hostBootSha256="c" * 64)
        a = dict(arm="fawn-dawn", browserSource=source, compositorDigest="cpu",
                 gpuProcessDrm={"devicesSeen": ["i915@0000:00:02.0"]})
        b = dict(a, arm="fawn-doe")
        match = runner["physical_pair_mismatch"]
        self.assertIsNone(match(a, b))
        for devices in ([], ["i915"], ["i915@0000:00:02.1"], ["i915@0000:00:02.0", "amdgpu@0000:03:00.0"]):
            self.assertIsNotNone(match(a, dict(b, gpuProcessDrm={"devicesSeen": devices})))
        self.assertIsNotNone(match(a, dict(b, browserSource=dict(source, hostBootSha256="d" * 64))))
        self.assertIsNotNone(match(a, dict(b, browserSource=dict(source, chromeSha256="a" * 64))))
        self.assertIsNone(match(dict(a, arm="stock-dawn"), dict(b, arm="stock-helper", gpuProcessDrm=None)))

    def test_wrong_target_trace_loss_software_and_unbalanced_plan(self):
        wave = dict(arm="stock-dawn", width=1, repeat=1, observer="on")
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            make_row(root, wave, 10)
            for file, fields in [("stock-observer.complete", {"dataLossOccurred": True}),
                                 ("stock-observer.scope", {"targetId": "another tab"}),
                                 ("progress", {"softwareSandbox": {"gpuSandboxed": True}}),
                                 ("progress", {"gpu": {"vendor": "lavapipe", "isFallbackAdapter": False}})]:
                with self.subTest(file=file, fields=fields):
                    path = root / (file + ".json")
                    original = runner["read"](path)
                    record(root, file, dict(original, **fields))
                    self.assertEqual(runner["summarize_row"](root, wave, 1)["status"], "unqualified")
                    record(root, file, original)
            record(root, "plan", [wave, wave])
            with self.assertRaisesRegex(ValueError, "pair"):
                runner["summarize"](root)
