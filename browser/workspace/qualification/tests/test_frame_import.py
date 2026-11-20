import copy
import importlib.util
import json
import runpy
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("frame_import", Path(__file__).resolve().parents[1] / "check-frame-import.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    scope = dict(frameId="F" * 32, loaderId="D" * 32, executionContextId=3, captureId="surface1",
                 grantId="read", grantRevision=5, ordinal=2, mediaTimestampUs=987)
    source = dict(component=65, sourceEpoch=0, version=1)
    selected = dict(context=3, frame=scope["frameId"], loader=scope["loaderId"], **source)
    stream, target = "1" * 40, "2" * 40

    def event(name, pid, at, **fields):
        return dict(cat=module.CATEGORY, ph="I", name="FawnResident" + name, pid=pid, ts=at, args=fields)
    events = [
        event("TrackRestriction", 10, 1, context=3, track="A" * 32, stream=stream, target=target, **source),
        event("BrowserRestriction", 9, 2, stream=stream, target=target, version=1, accepted=True, restriction=True),
        event("FrameImported", 10, 3, mediaTimestampUs=123, **selected),
        event("FrameImported", 10, 4, mediaTimestampUs=987, **selected),
        event("StreamClosed", 9, 5, stream=stream),
    ]
    return scope, events


class FrameImportTest(unittest.TestCase):
    def test_same_native_frame_and_permission(self):
        scope, events = fixture()
        module.trace_input(events[::-1], scope, 9)

    def test_lookalikes_and_scope_changes_refuse(self):
        changes = {
            "page-mark": lambda e: e[3].update(cat="blink.user_timing"),
            "other-loader": lambda e: e[3]["args"].update(loader="C"*32),
            "other-document": lambda e: e[3]["args"].update(frame="C"*32),
            "other-world": lambda e: e[3]["args"].update(context=4),
            "other-component": lambda e: e[3]["args"].update(component=72),
            "cloned-untagged": lambda e: e[3]["args"].update(component=0),
            "other-source-epoch": lambda e: e[3]["args"].update(sourceEpoch=1),
            "other-frame-version": lambda e: e[3]["args"].update(version=2),
            "stale-timestamp": lambda e: e[3]["args"].update(mediaTimestampUs=111),
            "same-timestamp": lambda e: e[2]["args"].update(mediaTimestampUs=987),
            "reordered-frame": lambda e: e[2].update(ts=4.5),
            "wrong-renderer": lambda e: e[3].update(pid=15),
            "renderer-permit": lambda e: e[1].update(pid=10),
            "refused-permit": lambda e: e[1]["args"].update(accepted=False),
            "different-target": lambda e: e[1]["args"].update(target="1"*40),
            "different-permit-version": lambda e: e[1]["args"].update(version=3),
            "revoked": lambda e: e[4].update(ts=3.5),
            "late-permit": lambda e: e[1].update(ts=4),
            "extra-permit": lambda e: e.append(copy.deepcopy(e[1])),
            "second-track": lambda e: e.append(copy.deepcopy(e[0])),
        }
        for name, change in changes.items():
            with self.subTest(case=name):
                scope, events = fixture()
                change(events)
                with self.assertRaises(ValueError):
                    module.trace_input(events, scope, 9)

    def test_owned_row_only_and_timing_stays_closed(self):
        scope, events = fixture()
        expected = {k: v for k, v in scope.items() if k not in ("ordinal", "mediaTimestampUs")}
        sha = "1" * 64
        data = {"browser-source": {"chromeSha256": sha},
                "progress": dict(frameImportScope=expected, surfaceId="surface1", browserPid=9,
                                 selection={"executable": {"sha256": sha}}),
                "exit": {"allReady": True, "exit": 0, "waveTimeout": False},
                "outcome": dict(verified=True, imageCorrect=True, knownEffects=1, committedRevision="B-r2", diagnosticKind="frame-import", diagnosticOnly=True,
                                frameImportTimestampUs=987, frameImportOrdinal=2),
                "frame-import.scope": scope, "frame-import.trace": {"traceEvents": events},
                "frame-import.complete": {"dataLossOccurred": False}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            def put(name, value):
                (path / (name + ".json")).write_text(json.dumps(value))
            for name, value in data.items():
                put(name, value)
            self.assertEqual(module.check(path), dict(restrictedFrameImportVerified=True,
                                                      nativeDispatchVerified=False, latencyQualified=False))
            summarize = runpy.run_path(str(Path(__file__).resolve().parents[1] / "resident-comparison.py"))["summarize_row"]
            wave = {"arm": "fawn-doe", "repeat": 1, "width": 1}
            row = summarize(path, wave, 1)
            self.assertTrue(row["frameImport"]["restrictedFrameImportVerified"])
            self.assertEqual(row["status"], "unqualified")
            self.assertIsNone(row["pageChangeToVerifiedMidpointMs"])
            for name, changed in [("frame-import.scope", dict(scope, loaderId="C"*32)),
                                  ("frame-import.complete", {"dataLossOccurred": True}),
                                  ("exit", dict(data["exit"], waveTimeout=True)),
                                  ("browser-source", {"chromeSha256": "2"*64}),
                                  ("outcome", dict(data["outcome"], diagnosticKind="gdb"))]:
                with self.subTest(file=name):
                    put(name, changed)
                    with self.assertRaises(ValueError):
                        module.check(path)
                    put(name, data[name])


if __name__ == "__main__":
    unittest.main()
