import copy
import json
from pathlib import Path
import runpy
import tempfile
import unittest

QUAL = Path(__file__).resolve().parents[1]
runner = runpy.run_path(str(QUAL / "resident-comparison.py"))
fixtures = runpy.run_path(str(Path(__file__).with_name("test_observer_pair.py")))
stock = json.loads((QUAL / "receipts/20261120-stock-native-planes/workflow-final.json").read_text())


def refresh(frame):
    return dict(
        priorSession="retired",
        newSession=frame["sessionId"],
        grantId=frame["grantId"],
        priorRevision=frame["grantRevision"] - 1,
        newRevision=frame["grantRevision"],
        staleSessionRefused=True,
        newFrameRgbaBytes=frame["width"] * frame["height"] * 4,
        newFrameHostCopyBytes=frame["width"] * frame["height"] * 4,
        permissionRequests=1,
    )


class Compare(unittest.TestCase):
    def test_schedule_and_selected_paths(self):
        plan = runner["make_plan"](2, [1], sparse_controls=True)
        self.assertEqual(len(plan), 22)
        self.assertEqual([w["arm"] for w in plan[:2]], ["fawn-dawn", "fawn-doe"])
        self.assertEqual(len({runner["run_id"](w, 1) for w in plan}), len(plan))
        for w in plan:
            selected = runner["capture_selection"](w, "webgpu")
            self.assertEqual(selected.get("BLITZEN_TEST_LINKED_SPARSE_ORACLE") == "1", w.get("oracle", False))
            self.assertEqual(
                selected.get("BLITZEN_TEST_FAWN_PLANE_EVIDENCE") == "1", w["arm"].startswith("fawn")
            )
            if w["arm"] == "stock-js-i420":
                self.assertEqual(selected["BLITZEN_TEST_COMPOSITOR_DIGEST"], "cpu")
                self.assertEqual(selected["BLITZEN_REVIEW_COMPOSITOR_TRANSPORT"], "stock-i420")
            if w["arm"] == "stock-dawn":
                self.assertEqual(selected["BLITZEN_REVIEW_COMPOSITOR_TRANSPORT"], "external-texture")
        self.assertEqual(len(runner["make_plan"](2, [1])), 8)
        with self.assertRaises(ValueError):
            runner["capture_selection"]({**plan[0], "observer": "on"}, "cpu")

    def test_stock_frame_accounting_and_refusal(self):
        outcome = copy.deepcopy(stock)
        a, b = [outcome["pixelAuthority"]["fresh"][k] for k in ("reviewed", "checked")]
        wave = dict(arm="stock-js-i420", sparseControls=True)
        progress = dict(surfaceId=a["sessionId"])
        # The real retained workflow predates proof of renewed scope. Never promote it.
        with self.assertRaises((KeyError, ValueError)):
            runner["capture_control"](wave, progress, outcome)
        self.assertEqual(outcome["frameHostCopyBytes"], 43684)
        self.assertEqual(outcome["stockPlaneExportBytes"], 27300)
        outcome.update(
            grantRefresh=refresh(a), stockSourceImageReadBytesBeforeCapture=534164032, digestMode="cpu"
        )
        a.update(stockPlaneSource=True, stockPlaneSparseTiles=True)
        b.update(stockPlaneEqualityReused=True)
        complete = runner["capture_control"](wave, progress, outcome)
        self.assertEqual(complete["sourceImageReadBytesBeforeCapture"], 534164032)
        for field, value in [("stockPlaneSource", False), ("stockPlaneSparseTiles", False)]:
            bad = copy.deepcopy(outcome)
            bad["pixelAuthority"]["fresh"]["reviewed"][field] = value
            with self.assertRaises(ValueError):
                runner["capture_control"](wave, progress, bad)
        bad = copy.deepcopy(outcome)
        bad["grantRefresh"]["priorRevision"] = bad["grantRefresh"]["newRevision"]
        with self.assertRaises(ValueError):
            runner["capture_control"](wave, progress, bad)
        outcome["frameHostCopyBytes"] += 534164032
        with self.assertRaises(ValueError):
            runner["capture_control"](wave, progress, outcome)

    def test_oracle_requires_same_image_grant_sample_and_sparse_pixels(self):
        w = dict(arm="fawn-dawn", repeat=0, width=1, sparseControls=True, oracle=True)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            fixtures["make_row"](path, dict(arm="fawn-dawn", observer="off"), 9)
            read = runner["read"]
            save = lambda n, v: runner["write"](path / (n + ".json"), v)
            outcome = read(path / "outcome.json")
            progress = read(path / "progress.json")
            outcome.update(
                compositorTransport="external-texture",
                diagnosticOnly=True,
                nativePlaneSnapshotSamples=2,
                nativePlaneSnapshotCopyBytes=86,
            )
            fresh = outcome["pixelAuthority"]["fresh"]
            a, b = fresh["reviewed"], fresh["checked"]
            a["nativePlaneSparseTilesReused"] = True
            b["nativePlaneEqualityReused"] = True
            outcome["grantRefresh"] = refresh(a)
            a["frameHostCopyBytes"] = 4
            b["frameHostCopyBytes"] = 0

            def observed(frame):
                return {
                    **{k: frame[k] for k in ("sessionId", "grantId", "grantRevision", "mediaTimestampUs")},
                    "hashes": frame["tileHashes"],
                    "producerCopyBytes": frame["frameHostCopyBytes"],
                    "producerHashBytes": frame["canonicalHashedBytes"],
                    "oracleCopyBytes": frame["width"] * frame["height"] * 4,
                }

            oa, ob = observed(a), observed(b)
            oa.update(usedNativeTiles=True, changed=[0], candidates=[0], rejectedTileReads=0)
            ob.update(usedNativeEquality=True, changed=[])
            outcome["linkedSparseOracle"] = dict(
                schema="blitzen.same-frame-sparse-correctness/v1",
                sameFrameOracle=True,
                maskSource="linked-restricted-reader",
                diagnosticOnly=True,
                verifiedFrames=2,
                oracleCopyBytes=2 * oa["oracleCopyBytes"],
                oracleHashBytes=2 * oa["oracleCopyBytes"],
                reviewed=oa,
                checked=ob,
            )
            save("outcome", outcome)
            control = runner["oracle_report"](path, w)
            self.assertTrue(control["verified"], control)
            self.assertFalse(control["latencyQualified"])
            for key, value in [("mediaTimestampUs", 0), ("sessionId", "replaced"), ("candidates", [])]:
                bad = copy.deepcopy(outcome)
                bad["linkedSparseOracle"]["reviewed"][key] = value
                save("outcome", bad)
                self.assertFalse(runner["oracle_report"](path, w)["verified"])
            save("outcome", outcome)
            changed = read(path / "browser-source.json")
            changed["chromeSha256"] = "1" * 64
            save("browser-source", changed)
            self.assertFalse(runner["oracle_report"](path, w)["verified"])
