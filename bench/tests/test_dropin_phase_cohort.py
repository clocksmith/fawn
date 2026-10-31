from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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


class BackendBuildTest(unittest.TestCase):
    def build_argv(self, backend: str, upload_split: bool = False) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libwebgpu.so"
            library.write_bytes(b"library")
            output = Path(directory) / "harness"
            output.write_bytes(b"binary")
            with mock.patch.object(cohort.subprocess, "run") as run, mock.patch("builtins.print"):
                cohort.build(library, output, "cc", backend, upload_split)
            return run.call_args.args[0]

    def test_vulkan_default_adds_no_define(self) -> None:
        self.assertFalse(any(arg.startswith("-DDOE_PHASE_BACKEND") for arg in self.build_argv("vulkan")))

    def test_metal_selects_the_metal_backend(self) -> None:
        self.assertIn("-DDOE_PHASE_BACKEND_METAL", self.build_argv("metal"))

    def test_upload_split_is_opt_in(self) -> None:
        self.assertNotIn("-DDOE_PHASE_UPLOAD_SPLIT", self.build_argv("metal"))
        self.assertIn("-DDOE_PHASE_UPLOAD_SPLIT", self.build_argv("metal", True))

    def test_dynamic_provider_is_loaded_instead_of_linked(self) -> None:
        with tempfile.TemporaryDirectory(prefix="phase provider ") as directory:
            library = Path(directory) / "libwebgpu.dylib"
            library.write_bytes(b"library")
            output = Path(directory) / "harness"
            output.write_bytes(b"binary")
            with mock.patch.object(cohort.sys, "platform", "darwin"), \
                    mock.patch.object(cohort.subprocess, "run") as run, \
                    mock.patch("builtins.print"):
                cohort.build(library, output, "cc", "metal", load_provider=True)
            argv = run.call_args.args[0]
            self.assertNotIn(str(library.resolve()), argv)
            self.assertIn('-DDOE_PHASE_LOAD_PROVIDER="' + str(library.resolve()) + '"', argv)

    def test_dynamic_provider_rejects_unsupported_platform_or_backend(self) -> None:
        with mock.patch.object(cohort.subprocess, "run") as run:
            for platform, backend in (("linux", "metal"), ("darwin", "vulkan")):
                with mock.patch.object(cohort.sys, "platform", platform), self.assertRaises(SystemExit):
                    cohort.build(Path("provider"), Path("harness"), "cc", backend, load_provider=True)
            run.assert_not_called()


class WindowsBuildTest(unittest.TestCase):
    def test_links_the_import_library_beside_the_dll(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "webgpu_doe_c.dll"
            library.write_bytes(b"library")
            library.with_suffix(".lib").write_bytes(b"imports")
            output = Path(directory) / "harness.exe"
            output.write_bytes(b"binary")
            with mock.patch.object(cohort.os, "name", "nt"), mock.patch.object(cohort.subprocess, "run") as run, \
                    mock.patch("builtins.print"):
                cohort.build(library, output, "clang-cl", "vulkan")
            argv = run.call_args.args[0]
        self.assertIn(str(library.with_suffix(".lib").resolve()), argv)
        self.assertIn(f"/Fe{output}", argv)
        self.assertNotIn("-Wl,-rpath", " ".join(argv))

    def test_refuses_a_harness_away_from_its_dll(self) -> None:
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as elsewhere:
            library = Path(directory) / "webgpu_doe_c.dll"
            library.write_bytes(b"library")
            library.with_suffix(".lib").write_bytes(b"imports")
            away = Path(elsewhere) / "harness.exe"
            with mock.patch.object(cohort.os, "name", "nt"), mock.patch.object(cohort.subprocess, "run") as run:
                with self.assertRaises(SystemExit):
                    cohort.build(library, away, "clang-cl", "vulkan")
            run.assert_not_called()


class CompleteMetricTest(unittest.TestCase):
    def test_provider_load_is_reported_as_nested_setup_cost(self) -> None:
        result = {"setupNs": 9_000_000, "providerLoadNs": 3_000_000,
                  "frameworkImagesBeforeSetup": 0, "setupImagesLoaded": 300,
                  "maxRssKiB": 1024, "phases": {name: [1000, 1000] for name in cohort.LOOP_PHASES}}
        metrics = cohort.process_metrics({"processWallNs": 1, "childCpuMs": 1.0, "result": result})
        self.assertEqual(metrics["setupNs"], 9)
        self.assertEqual(metrics["providerLoadNs"], 3)
        self.assertEqual(metrics["frameworkImagesBeforeSetup"], 0)
        self.assertEqual(metrics["setupImagesLoaded"], 300)

    def test_complete_sums_preparation_operations_and_teardown(self) -> None:
        phases = {name: [1000, 1000] for name in cohort.LOOP_PHASES}
        phases["operation"] = [3_000_000, 1_000_000]
        result = {"setupNs": 9_000_000, "compileNs": 1_000_000, "pipelineNs": 2_000_000, "resourcesNs": 500_000,
                  "teardownNs": 250_000, "processInMainNs": 1, "maxRssKiB": 1024, "phases": phases}
        metrics = cohort.process_metrics({"processWallNs": 1, "childCpuMs": 1.0, "result": result})
        self.assertAlmostEqual(metrics["complete"], 7.75)
        self.assertNotIn("warm.uploadDoe", metrics)

    def test_split_upload_phases_become_metrics(self) -> None:
        phases = {name: [1000, 1000] for name in cohort.LOOP_PHASES}
        phases["uploadSource"], phases["uploadDoe"] = [400, 300], [600, 700]
        result = {"maxRssKiB": 1024, "phases": phases}
        metrics = cohort.process_metrics({"processWallNs": 1, "childCpuMs": 1.0, "result": result})
        self.assertAlmostEqual(metrics["warm.uploadSource"], 0.0003)
        self.assertAlmostEqual(metrics["warm.uploadDoe"], 0.0007)


if __name__ == "__main__":
    unittest.main()
