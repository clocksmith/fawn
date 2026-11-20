from pathlib import Path
import runpy
import unittest

check = runpy.run_path(str(Path(__file__).resolve().parents[1] / "resident-comparison.py"))["canonical_pixel_evidence"]


def recorded_pixels(surface):
    def frame(stamp, first):
        return dict(sessionId=surface, grantId="selected-region", grantRevision=3,
                    mediaTimestampUs=stamp, sourcePixelFormat="I420", width=128, height=64, tileSize=64,
                    tileHashes=[first * 64, "b" * 64], frameHostCopyBytes=32768, canonicalHashedBytes=32768,
                    canonicalRehashedTiles=2, externalHintMissedChange=False)

    proof = dict(schema="blitzen.canonical-capture/v1", representation="video-frame-copy-to-srgb-rgba8",
                 stale=dict(reviewed=frame(8, "a"), checked=frame(10, "c"), changedTiles=[0],
                            refusal="stale-observation", effectsBefore=0, effectsAfter=0),
                 fresh=dict(reviewed=frame(15, "e"), checked=frame(17, "e"), effectCount=1,
                            reconciliation="effect-present"))
    return dict(pixelAuthority=proof, frameHostCopyBytes=65536, canonicalHashedBytes=65536,
                canonicalRehashedTiles=4, externalHintMissedChanges=0)


class CanonicalFrames(unittest.TestCase):
    def test_changed_shadow_and_matching_fresh_capture(self):
        data = dict(knownEffects=1, **recorded_pixels("selected"))
        result = check({"surfaceId": "selected"}, data)
        self.assertEqual((result["staleTiles"], result["freshEffectCount"]), (1, 1))

    def test_requires_the_actual_changed_tile_and_refusal(self):
        for field, value in (("refusal", "accepted"), ("changedTiles", [1]), ("effectsAfter", 1)):
            with self.subTest(field=field):
                data = dict(knownEffects=1, **recorded_pixels("selected"))
                data["pixelAuthority"]["stale"][field] = value
                with self.assertRaises(ValueError):
                    check({"surfaceId": "selected"}, data)

    def test_rejects_unrelated_or_old_frames_and_undercharged_readbacks(self):
        for field, value in (("sessionId", "other"), ("grantRevision", 4), ("tileHashes", ["b" * 64]),
                             ("mediaTimestampUs", 15), ("sourcePixelFormat", "RGBA"),
                             ("externalHintMissedChange", True), ("frameHostCopyBytes", 65536)):
            with self.subTest(field=field):
                data = dict(knownEffects=1, **recorded_pixels("selected"))
                data["pixelAuthority"]["fresh"]["checked"][field] = value
                with self.assertRaises(ValueError):
                    check({"surfaceId": "selected"}, data)
        data = dict(knownEffects=1, **recorded_pixels("neighbor"))
        with self.assertRaisesRegex(ValueError, "measured capture"):
            check({"surfaceId": "selected"}, data)

    def test_historical_receipt_is_not_current_pixel_evidence(self):
        with self.assertRaisesRegex(ValueError, "missing canonical"):
            check({"surfaceId": "selected"}, {"verified": True, "imageCorrect": True})


if __name__ == "__main__":
    unittest.main()
