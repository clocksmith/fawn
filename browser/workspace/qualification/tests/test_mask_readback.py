import copy
import hashlib
import json
from pathlib import Path
import re
import runpy
from tempfile import TemporaryDirectory
import unittest

DIRECTORY = Path(__file__).resolve().parents[1]
readback = runpy.run_path(str(DIRECTORY / "check-mask-readback.py"))
frame_fixture = runpy.run_path(str(Path(__file__).with_name("test_frame_import.py")))["fixture"]


def fixture():
    scope, events = frame_fixture()
    scope.update(width=513, height=513)
    sequence = 0
    assigned = {}

    def edge(key, op, owner, a=0, b=0, c=0, d=0):
        nonlocal sequence
        sequence += 1
        event = dict(
            name="FawnResidentResourceEdge",
            cat=readback["INPUT"]["CATEGORY"],
            ph="I",
            pid=10,
            tid=12,
            ts=3 + sequence / 100,
            args=dict(context=3, device=7, seq=sequence, op=op, owner=owner, a=a, b=b, c=c, d=d),
        )
        assigned[key] = event
        events.append(event)

    def conversion(key, frame, tex, external, base):
        nonlocal sequence
        sequence += 1
        frame.update(tid=12, ts=3 + sequence / 100)
        frame["args"].update(device=7, seq=sequence, external=external)
        encoder, gpu_pass, view, group, cmd = range(base, base + 5)
        edge(key + "pass", "pass", encoder, gpu_pass)
        edge(key + "shader", "set-kernel", gpu_pass, 1)
        edge(key + "view", "view", tex, view)
        edge(key + "group", "group", group, 2)
        edge(key + "external", "external", group, 0, external)
        edge(key + "dest", "texture", group, 1, view)
        edge(key + "bind", "set-group", gpu_pass, 0, group)
        edge(key + "dispatch", "dispatch", gpu_pass, 65, 65, 1)
        edge(key + "end", "end", gpu_pass)
        edge(key + "finish", "finish", encoder, cmd)
        edge(key + "submit", "submit", 42, cmd, 1)

    conversion("prev-", events[2], 30, 222, 50)
    conversion("curr-", events[3], 31, 333, 60)
    edge("uniform", "write", 42, 100, 0, 4)
    edge("pass", "pass", 70, 71)
    edge("shader", "set-kernel", 71, 2)
    edge("old-view", "view", 30, 73)
    edge("new-view", "view", 31, 74)
    edge("group", "group", 75, 4)
    edge("old", "texture", 75, 0, 73)
    edge("new", "texture", 75, 1, 74)
    edge("mask", "buffer", 75, 2, 80)
    edge("tile", "buffer", 75, 3, 100)
    edge("bind", "set-group", 71, 0, 75)
    edge("dispatch", "dispatch", 71, 65, 65, 1)
    edge("end", "end", 71)
    edge("copy", "copy", 70, 80, 81, 12)
    edge("finish", "finish", 70, 76)
    edge("submit", "submit", 42, 76, 1)
    edge("map", "map", 81, 1, 0, 12)
    edge("done", "map-ok", 81)
    return events, scope, assigned


class MaskReadbackTest(unittest.TestCase):
    def test_private_inputs_produce_only_the_narrow_webgpu_verdict(self):
        events, scope, edges = fixture()
        distractor = copy.deepcopy(edges["done"])
        distractor["args"].update(device=812, owner=835)
        result = readback["checked_mask"]([distractor, *reversed(events)], scope, 9)
        self.assertEqual(
            result,
            dict(
                restrictedFrameImportVerified=True,
                submittedTileDiffVerified=True,
                webGpuMaskReadMapSucceeded=True,
                maskBytes=12,
                nativeBackendCompletionVerified=False,
                latencyQualified=False,
            ),
        )

    def test_different_bindings_failure_and_interleaving_stay_unproven(self):
        changes = [
            ("prev-external", "b", 333),
            ("curr-external", "b", 222),
            ("curr-shader", "a", 0),
            ("curr-dest", "b", 901),
            ("curr-dispatch", "a", 1),
            ("prev-submit", "owner", 44),
            ("old-view", "owner", 31),
            ("new-view", "owner", 30),
            ("old", "b", 74),
            ("new", "a", 0),
            ("group", "a", 3),
            ("mask", "a", 3),
            ("tile", "b", 81),
            ("mask", "d", 1),
            ("tile", "c", 8),
            ("uniform", "c", 8),
            ("shader", "a", 1),
            ("bind", "b", 200),
            ("dispatch", "op", "unsupported-indirect"),
            ("dispatch", "a", 64),
            ("copy", "a", 81),
            ("copy", "d", 1),
            ("copy", "c", 4),
            ("copy", "b", 100),
            ("finish", "a", 64),
            ("submit", "a", 102),
            ("submit", "b", 2),
            ("submit", "owner", 40),
            ("map", "c", 4),
            ("done", "op", "map-failed"),
            ("done", "owner", 82),
            ("done", "context", 350),
        ]
        for key, field, value in changes:
            with self.subTest(key=key, field=field):
                events, scope, edges = fixture()
                edges[key]["args"][field] = value
                with self.assertRaises(ValueError):
                    readback["checked_mask"](events, scope, 9)
        for case in (
            "missing",
            "duplicate",
            "wrong-source",
            "wrong-thread",
            "page-mark",
            "drop-frame",
            "later-permission",
            "substituted-dimensions",
            "revoked-after-import",
            "invalid-clock",
        ):
            with self.subTest(case=case):
                events, scope, edges = fixture()
                if case == "missing":
                    events.remove(edges["submit"])
                if case == "duplicate":
                    events.append(copy.deepcopy(edges["map"]))
                if case == "wrong-source":
                    edges["map"].update(pid=9)
                if case == "wrong-thread":
                    edges["map"].update(tid=19)
                if case == "page-mark":
                    edges["done"].update(cat="blink.user_timing")
                if case == "drop-frame":
                    events.pop(2)
                if case == "later-permission":
                    events[1]["ts"] = 3.5
                if case == "substituted-dimensions":
                    scope.update(width=50)
                if case == "revoked-after-import":
                    events[4]["ts"] = edges["submit"]["ts"]
                if case == "invalid-clock":
                    edges["done"]["ts"] = events[2]["ts"]
                with self.assertRaises(ValueError):
                    readback["checked_mask"](events, scope, 9)

    def test_only_frozen_fixture_shaders_match(self):
        source = (DIRECTORY.parent / "src/adapters/resident-canvas.js").read_text()
        patch = (
            DIRECTORY.parents[2] / "browser/chromium/patches/resident-mask-dependency.patch"
        ).read_text()
        for name, length in (("code", 382), ("shader", 947)):
            wgsl = re.search("const " + name + " = `([^`]+)`;", source).group(1).encode()
            self.assertEqual(len(wgsl), length)
            self.assertIn(hashlib.sha256(wgsl).hexdigest().upper(), patch)

    def test_report_preserves_eligible_scope_and_usage_when_mask_fails(self):
        events, scope, edges = fixture()
        baseline = {
            k: v for k, v in scope.items() if k not in ("ordinal", "mediaTimestampUs", "width", "height")
        }
        sha = "e" * 64
        outcome = dict(
            verified=True,
            imageCorrect=True,
            knownEffects=1,
            committedRevision="B-r2",
            diagnosticKind="frame-import",
            diagnosticOnly=True,
            frameImportTimestampUs=987,
            frameImportOrdinal=2,
            frameImportDimensions=[513, 513],
            plannerCalls=5,
            modelSpendUsd=0.13,
        )
        files = {
            "browser-source": {"chromeSha256": sha},
            "progress": {
                "frameImportScope": baseline,
                "surfaceId": "surface1",
                "browserPid": 9,
                "selection": {"executable": {"sha256": sha}},
            },
            "exit": {"allReady": True, "exit": 0, "waveTimeout": False},
            "outcome": outcome,
            "frame-import.scope": scope,
            "frame-import.trace": {"traceEvents": events},
            "frame-import.complete": {"dataLossOccurred": False},
        }
        summarize = runpy.run_path(str(DIRECTORY / "resident-comparison.py"))["summarize_row"]
        with TemporaryDirectory() as folder:
            path = Path(folder)

            def save():
                for name, value in files.items():
                    (path / (name + ".json")).write_text(json.dumps(value))

            for succeeded in (True, False):
                if not succeeded:
                    events.remove(edges["done"])
                save()
                row = summarize(path, {"arm": "fawn-doe", "repeat": 1, "width": 1}, 1)
                self.assertTrue(row["frameImport"]["restrictedFrameImportVerified"])
                self.assertEqual(row["maskReadback"]["submittedTileDiffVerified"], succeeded)
                self.assertFalse(row["maskReadback"]["latencyQualified"])
                self.assertEqual(row["plannerCalls"], 5)
                self.assertEqual(row["modelSpendUsd"], 0.13)
                self.assertIsNone(row["pageChangeToVerifiedMidpointMs"])
                self.assertEqual(row["status"], "unqualified")


if __name__ == "__main__":
    unittest.main()
