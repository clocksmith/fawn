from __future__ import annotations

import struct
import unittest

from bench.lib import synthetic_assets
from bench.tools import dropin_phase_cohort as cohort


class DropinPhaseCohortTest(unittest.TestCase):
    def test_invert_and_copy_oracles_are_exact(self) -> None:
        data = struct.pack("<3I", 0, 0xFF000000, 0x12345678)
        a, b = cohort.oracle("invert", data)
        self.assertEqual(a, b)
        self.assertEqual(struct.unpack("<3I", a), (0x00FFFFFF, 0xFFFFFFFF, 0x12CBA987))
        self.assertEqual(cohort.oracle("copy", data), (data, data))

    def test_scale_add_admits_separate_and_fused_rounding(self) -> None:
        data = struct.pack("<2f", 0.1, 0.125)
        separate, fused = cohort.oracle("scale_add", data)
        x = cohort.f32(0.1)
        self.assertEqual(struct.unpack("<2f", separate)[0], cohort.f32(cohort.f32(x * 1.5) + 0.25))
        self.assertEqual(struct.unpack("<2f", fused)[0], cohort.f32(x * 1.5 + 0.25))
        self.assertEqual(struct.unpack("<2f", separate)[1], 0.4375)

    def test_copy_set0_matches_frozen_plan_sha(self) -> None:
        spec = cohort.WORKLOADS["buffer_upload_readback_exact_1mb"]
        data = synthetic_assets.generate_asset_bytes(generator="splitmix64_f32_nonzero_v1", seed=spec["seed"],
                                                     scale=spec["scale"], byte_length=spec["bytes"])
        a, b = cohort.oracle("copy", data)
        self.assertEqual(cohort.cross_check_frozen_plan("buffer_upload_readback_exact_1mb", spec, a, b, None),
                         "sha256Equals")
        with self.assertRaises(AssertionError):
            cohort.cross_check_frozen_plan("buffer_upload_readback_exact_1mb", spec, a[:-4] + b"\0\0\0\0", b, None)

    def test_frozen_oracle_is_required_when_plan_has_none(self) -> None:
        with self.assertRaises(AssertionError):
            cohort.cross_check_frozen_plan("vector_scale_add_262k", cohort.WORKLOADS["vector_scale_add_262k"],
                                           b"", b"", None)

    def test_bootstrap_interval_is_deterministic(self) -> None:
        values = [0.9, 1.0, 1.1, 0.95, 1.05, 0.98, 1.02]
        self.assertEqual(cohort.bootstrap_median_ci(values), cohort.bootstrap_median_ci(values))
        low, high = cohort.bootstrap_median_ci(values)
        self.assertLessEqual(low, high)


if __name__ == "__main__":
    unittest.main()
