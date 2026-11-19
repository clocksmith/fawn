"""Unit tests for the zero-trust C2 WebGPU graphics, Doppler receipt producer, and Chromium ownership gate."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from bench.gates.webgpu_graphics_campaign_gate import (
    CONTRACT_REL_PATH,
    evaluate_backend_campaign_receipt,
    evaluate_chromium_ownership_receipt,
    resolve_repo_root,
    validate_campaign_contract,
)
from bench.lib.bench_utils import load_json_object


def _load_doppler_producer(repo_root: Path):
    mod_path = repo_root / "bench/external-projects/doppler-generation/produce_c2_receipt.py"
    spec = importlib.util.spec_from_file_location("produce_c2_receipt", mod_path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _encode_test_png(width: int, height: int, delta: int = 0, uniform: bool = False) -> bytes:
    """Encode a valid non-interlaced 8-bit RGBA PNG for pixel-oracle unit tests."""

    def chunk(ctype: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(ctype + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    if uniform:
        row_a = b"\x00" + bytes((40, 80, 120, 255)) * width
        row_b = row_a
    else:
        px_a = bytes((min(255, 50 + delta), 100, 150, 255))
        px_b = bytes((min(255, 90 + delta), 140, 190, 255))
        half = width // 2
        rem = width - half * 2
        row_a = b"\x00" + (px_a + px_b) * half + px_a * rem
        row_b = b"\x00" + (px_b + px_a) * half + px_b * rem
    pair_count = height // 2
    raw = (row_a + row_b) * pair_count + (row_a if height % 2 else b"")
    compressed = zlib.compress(raw, level=1)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class WebgpuGraphicsCampaignGateTests(unittest.TestCase):
    """Verify zero-trust C2 contract, negative fixtures, A/A calibration, and Doppler producer."""

    def setUp(self) -> None:
        self.repo_root = resolve_repo_root()
        self.contract = load_json_object(self.repo_root / CONTRACT_REL_PATH)
        self.thresholds = load_json_object(
            self.repo_root / self.contract["methodologyThresholdsPath"]
        )
        self.min_samples = int(self.thresholds["claimabilityDefaults"]["localMinTimedSamples"])

    def _build_valid_vulkan_fixture(self, tmp: Path) -> dict:
        dawn_png_bytes = _encode_test_png(1920, 1080, delta=0)
        doe_png_bytes = _encode_test_png(1920, 1080, delta=1)
        dawn_png = tmp / "dawn-frame.png"
        doe_png = tmp / "doe-frame.png"
        dawn_png.write_bytes(dawn_png_bytes)
        doe_png.write_bytes(doe_png_bytes)

        doe_lib_sha = "d" * 64
        dawn_lib_sha = "a" * 64
        dawn_rev = "1" * 40

        rows = []
        for wspec in self.contract["workloads"]:
            wid = wspec["workloadId"]
            manifest = load_json_object(self.repo_root / wspec["sourceContractPath"])
            if wspec["family"] == "inference_compute":
                src_hashes = {f["path"]: f["sha256"] for f in manifest["model"]["files"]}
                case_map = {
                    c["id"]: {
                        "promptTokenIds": [2, 10, 20],
                        "generatedTokenIds": [30, 40, 1],
                        "decodedText": f"verified-{c['id']}",
                        "stopReason": "eos",
                    }
                    for c in manifest["cases"]
                }
                rows.append(
                    {
                        "workloadId": wid,
                        "observedSourceFileHashes": src_hashes,
                        "observedCases": copy.deepcopy(case_map),
                        "referenceCases": copy.deepcopy(case_map),
                        "dawnSamplesMs": [140.0 + (i % 3) for i in range(self.min_samples)],
                        "doeSamplesMs": [100.0 + (i % 2) for i in range(self.min_samples)],
                        "timingPhasesMs": {
                            "coldSetupMs": 320.0,
                            "commandPreparationMs": 12.0,
                            "cpuVisibleCompletionMs": 100.0,
                        },
                        "resources": {
                            "cpuTimeMs": 45.0,
                            "processRssBytes": 512_000_000,
                            "gpuAllocatedBytes": 256_000_000,
                            "failOrSkipCount": 0,
                        },
                    }
                )
            else:
                src_hashes = {f["path"]: f["sha256"] for f in manifest["immutableSourceFiles"]}
                rows.append(
                    {
                        "workloadId": wid,
                        "observedSourceFileHashes": src_hashes,
                        "timingMethod": "queue_completion_plus_displayed_canvas_png_capture",
                        "dawnSamplesMs": [16.5 + 0.1 * (i % 3) for i in range(self.min_samples)],
                        "doeSamplesMs": [14.0 + 0.1 * (i % 2) for i in range(self.min_samples)],
                        "timingPhasesMs": {
                            "coldSetupMs": 180.0,
                            "commandPreparationMs": 2.4,
                            "cpuVisibleCompletionMs": 11.2,
                            "displayedPresentationMs": 14.0,
                        },
                        "resources": {
                            "cpuTimeMs": 18.0,
                            "processRssBytes": 420_000_000,
                            "gpuAllocatedBytes": 192_000_000,
                            "failOrSkipCount": 0,
                        },
                        "frameComparisons": [
                            {
                                "frameNumber": fn,
                                "dawnPngPath": str(dawn_png),
                                "doePngPath": str(doe_png),
                                "submissionSequence": fn,
                            }
                            for fn in manifest["captureFrames"]["pinnedFrameNumbers"]
                        ],
                    }
                )

        return {
            "backendId": "amd-vulkan",
            "observedBackends": ["amd-vulkan"],
            "dawnBuild": {
                "dawnRevision": dawn_rev,
                "browserExecutableSha256": "b" * 64,
                "dawnLibrarySha256": dawn_lib_sha,
                "validationMode": "enabled-default",
                "cachePolicy": "per-process-empty",
                "warmupFrames": 6,
                "actualBrowserArgs": ["--enable-unsafe-webgpu", "--use-angle=vulkan", "--enable-features=Vulkan"],
            },
            "doeBuild": {
                "dawnBaselineRevision": dawn_rev,
                "browserExecutableSha256": "c" * 64,
                "expectedDoeLibrarySha256": doe_lib_sha,
                "validationMode": "enabled-default",
                "cachePolicy": "per-process-empty",
                "warmupFrames": 6,
                "actualBrowserArgs": ["--enable-unsafe-webgpu", "--use-angle=vulkan", "--enable-features=Vulkan"],
                "observedLoadedLibraries": [
                    {"path": "/opt/doe/libwebgpu_doe.so", "sha256": doe_lib_sha},
                ],
            },
            "aaControlSamplesMs": {
                "A": [14.0, 14.1, 13.9, 14.0, 14.05, 13.95, 14.02, 13.98],
                "B": [14.05, 14.1, 13.95, 14.0, 14.08, 13.98, 14.04, 14.01],
            },
            "workloadRows": rows,
            "tailObligations": [],
        }

    def test_contract_pins_four_real_third_party_graphics_apps_with_sandbox_and_surface(self) -> None:
        errors = validate_campaign_contract(self.repo_root)
        self.assertEqual(errors, [])

    def test_contract_rejects_no_sandbox_or_disabled_vulkan_surface_in_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            for rel in (
                CONTRACT_REL_PATH,
                self.contract["strategyAuthorityPath"],
                self.contract["thesisAuthorityPath"],
                self.contract["methodologyThresholdsPath"],
                self.contract["integratedComparisonPath"],
                self.contract["dopplerReceiptProducerPath"],
                self.contract["preparationToolPath"],
                self.contract["captureHarnessPath"],
                self.contract["chromiumOwnershipGate"]["maintainabilityAuthorityPath"],
                "reports/browser-compiler/20261004/three-screen/dependencies.json",
            ):
                src = self.repo_root / rel
                dst = tmp / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(src.read_bytes())
            for w in self.contract["workloads"]:
                for p_rel in (w["sourceContractPath"], w["oracle"]["referencePath"]):
                    src = self.repo_root / p_rel
                    dst = tmp / p_rel
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_bytes(src.read_bytes())

            bad_manifest_path = tmp / "bench/external-projects/webgpu-graphics-campaigns/three-fog-scattering.workload.json"
            m_obj = json.loads(bad_manifest_path.read_text(encoding="utf-8"))
            m_obj["launchPolicy"]["platformBrowserArgs"]["linux"].extend(["--no-sandbox", "--disable-vulkan-surface"])
            bad_manifest_path.write_text(json.dumps(m_obj), encoding="utf-8")

            errs = validate_campaign_contract(tmp)
            self.assertTrue(any("forbidden graphics launch flag" in e for e in errs), errs)

    def test_vulkan_receipt_passes_only_with_real_pngs_hashes_and_8_process_aa_samples(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            receipt = self._build_valid_vulkan_fixture(tmp)
            self.assertEqual(
                evaluate_backend_campaign_receipt(
                    self.contract, self.thresholds, receipt, repo_root=self.repo_root, evidence_base_dir=tmp
                ),
                [],
            )

    def test_negative_fixtures_reject_forged_flags_hidden_surface_blank_presentation_dawn_fallback_and_bad_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            base = self._build_valid_vulkan_fixture(tmp)

            # Negative 1: Forged/forbidden sandbox flag in receipt actualBrowserArgs
            neg_flags = copy.deepcopy(base)
            neg_flags["doeBuild"]["actualBrowserArgs"].append("--no-sandbox")
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, neg_flags, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("forbidden flag '--no-sandbox'" in e for e in errs), errs)

            # Negative 2: Hidden surface / headless flag in receipt actualBrowserArgs
            neg_hidden = copy.deepcopy(base)
            neg_hidden["doeBuild"]["actualBrowserArgs"].append("--disable-vulkan-surface")
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, neg_hidden, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("forbidden flag '--disable-vulkan-surface'" in e for e in errs), errs)

            # Negative 3: Missing or blank/uniform canvas presentation
            blank_png = tmp / "blank.png"
            blank_png.write_bytes(_encode_test_png(1920, 1080, uniform=True))
            neg_blank = copy.deepcopy(base)
            three_row = next(r for r in neg_blank["workloadRows"] if r["workloadId"] == "three_webgpu_fog_scene")
            three_row["frameComparisons"][0]["doePngPath"] = str(blank_png)
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, neg_blank, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("blank or uniform" in e for e in errs), errs)

            # Negative 4: Dawn fallback in process maps despite self-reported fallbackOccurred=False
            neg_fallback = copy.deepcopy(base)
            neg_fallback["fallbackOccurred"] = False
            neg_fallback["doeBuild"]["observedLoadedLibraries"].append(
                {"path": "/usr/lib/libwebgpu_dawn.so", "sha256": "f" * 64}
            )
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, neg_fallback, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("fallback to Dawn" in e for e in errs), errs)

            # Negative 5: Bad Doppler tokens / stop reason AND corrupted RGBA pixels
            corrupt_png = tmp / "corrupt.png"
            corrupt_png.write_bytes(_encode_test_png(1920, 1080, delta=45))
            neg_outputs = copy.deepcopy(base)
            neg_outputs["workloadRows"][0]["oraclePassed"] = True
            neg_outputs["workloadRows"][0]["observedCases"]["short_prompt_long_output"]["stopReason"] = "length"
            t_row = next(r for r in neg_outputs["workloadRows"] if r["workloadId"] == "three_webgpu_fog_scene")
            t_row["oraclePassed"] = True
            t_row["frameComparisons"][0]["doePngPath"] = str(corrupt_png)
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, neg_outputs, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("exact token/text oracle mismatch" in e for e in errs), errs)
            self.assertTrue(any("RGBA diff" in e for e in errs), errs)

    def test_aa_calibration_rejects_thin_2_repeat_or_noisy_bootstrap_controls(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            thin = self._build_valid_vulkan_fixture(tmp)
            thin["aaControlSamplesMs"] = {"A": [14.0, 14.1], "B": [14.0, 14.1]}
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, thin, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("requires >= 8 positive samples" in e for e in errs), errs)

            noisy = self._build_valid_vulkan_fixture(tmp)
            noisy["aaControlSamplesMs"] = {
                "A": [11.0, 11.2, 18.5, 19.0, 11.1, 18.8, 11.3, 19.2],
                "B": [14.0, 14.1, 14.0, 14.2, 13.9, 14.1, 14.0, 14.2],
            }
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, noisy, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("A/A bootstrap 95% CI" in e for e in errs), errs)

    def test_suspicious_10x_speedup_requires_on_disk_fairness_audit(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            receipt = self._build_valid_vulkan_fixture(tmp)
            row0 = receipt["workloadRows"][0]
            row0["dawnSamplesMs"] = [1500.0 for _ in range(self.min_samples)]
            row0["doeSamplesMs"] = [100.0 for _ in range(self.min_samples)]
            row0["fairnessAuditPassed"] = True

            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, receipt, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("fairnessAuditArtifactPath" in e for e in errs), errs)

            audit_file = tmp / "doppler-fairness.json"
            audit_bytes = json.dumps(
                {
                    "workloadId": row0["workloadId"],
                    "attributedMechanism": "eliminated per-token staging buffer map stall",
                    "verifiedCompletedDispatches": 192,
                }
            ).encode("utf-8")
            audit_file.write_bytes(audit_bytes)
            row0["fairnessAuditArtifactPath"] = str(audit_file)
            row0["fairnessAuditSha256"] = _sha256_bytes(audit_bytes)
            self.assertEqual(
                evaluate_backend_campaign_receipt(
                    self.contract, self.thresholds, receipt, repo_root=self.repo_root, evidence_base_dir=tmp
                ),
                [],
            )

    def test_metal_requires_frozen_vulkan_file_and_rejects_tail_regression_from_raw_samples(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            vk_receipt = self._build_valid_vulkan_fixture(tmp)
            vk_path = tmp / "frozen-vulkan.json"
            vk_bytes = json.dumps(vk_receipt).encode("utf-8")
            vk_path.write_bytes(vk_bytes)

            metal = copy.deepcopy(vk_receipt)
            metal["backendId"] = "apple-metal"
            metal["observedBackends"] = ["apple-metal"]
            metal["dawnBuild"]["actualBrowserArgs"] = ["--enable-unsafe-webgpu", "--use-angle=metal"]
            metal["doeBuild"]["actualBrowserArgs"] = ["--enable-unsafe-webgpu", "--use-angle=metal"]
            metal["priorFrozenCheckpointPath"] = str(vk_path)
            metal["priorFrozenCheckpointSha256"] = _sha256_bytes(vk_bytes)

            adverse_file = tmp / "adverse-metal-onnx.json"
            adverse_bytes = b'{"adverseMetalP99Regression":true}\n'
            adverse_file.write_bytes(adverse_bytes)

            dawn_tail = [10.0 for _ in range(self.min_samples)]
            doe_tail = [6.0 for _ in range(self.min_samples - 2)] + [40.0, 45.0]
            metal["tailObligations"] = [
                {
                    "obligationId": "onnx-runtime-metal-tail-p95-p99",
                    "adverseRunPath": str(adverse_file),
                    "adverseRunSha256": _sha256_bytes(adverse_bytes),
                    "dawnTailSamplesMs": dawn_tail,
                    "doeTailSamplesMs": doe_tail,
                    "disposition": "fixed-on-current-code",
                }
            ]
            errs = evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, metal, repo_root=self.repo_root, evidence_base_dir=tmp
            )
            self.assertTrue(any("mean cannot hide tail loss" in e for e in errs), errs)

            metal["tailObligations"][0]["disposition"] = "unqualified-scope-disclosed"
            metal["tailObligations"][0]["unqualifiedScope"] = "ONNX dynamic-shape gather p95/p99 on M-series Metal"
            self.assertEqual(
                evaluate_backend_campaign_receipt(
                    self.contract, self.thresholds, metal, repo_root=self.repo_root, evidence_base_dir=tmp
                ),
                [],
            )

    def test_chromium_ownership_verifies_png_process_maps_and_disabled_doe_failure(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            pres_png = tmp / "presented.png"
            pres_png.write_bytes(_encode_test_png(64, 64, delta=3))
            maint_file = tmp / "maint.json"
            maint_bytes = b'{"patchFiles":4}\n'
            maint_file.write_bytes(maint_bytes)
            doe_sha = "e" * 64

            valid = {
                "positiveRun": {
                    "actualBrowserArgs": ["--enable-unsafe-webgpu", "--use-angle=vulkan"],
                    "exitCode": 0,
                    "expectedDoeLibrarySha256": doe_sha,
                    "observedLoadedLibraries": [{"path": "/opt/fawn/libwebgpu_doe.so", "sha256": doe_sha}],
                    "lifecycleTrace": [
                        {"stage": "device_creation", "ownerLibrarySha256": doe_sha, "handleOrSequence": "dev-1"},
                        {"stage": "shader_compilation", "ownerLibrarySha256": doe_sha, "handleOrSequence": "sh-1"},
                        {"stage": "submission", "ownerLibrarySha256": doe_sha, "handleOrSequence": 1},
                        {"stage": "mapped_readback", "ownerLibrarySha256": doe_sha, "handleOrSequence": "rb-1"},
                        {
                            "stage": "canvas_presentation",
                            "ownerLibrarySha256": doe_sha,
                            "handleOrSequence": 1,
                            "presentedPngPath": str(pres_png),
                        },
                        {"stage": "destruction", "ownerLibrarySha256": doe_sha, "handleOrSequence": "dev-1"},
                    ],
                },
                "disabledDoeNegativeControlRun": {
                    "disableMechanism": "FAWN_DISABLE_DOE=1",
                    "exitCode": 1,
                    "observedWebgpuError": "Forced Doe runtime disabled; Dawn fallback forbidden",
                    "presentedFrames": 0,
                    "observedLoadedLibraries": [],
                },
                "maintainabilityReceiptPath": str(maint_file),
                "maintainabilityReceiptSha256": _sha256_bytes(maint_bytes),
                "ranThreeWayComparison": False,
            }
            self.assertEqual(evaluate_chromium_ownership_receipt(self.contract, valid, evidence_base_dir=tmp), [])

            broken = copy.deepcopy(valid)
            broken["positiveRun"]["actualBrowserArgs"].append("--no-sandbox")
            broken["disabledDoeNegativeControlRun"]["exitCode"] = 0
            broken["disabledDoeNegativeControlRun"]["observedWebgpuError"] = ""
            broken["disabledDoeNegativeControlRun"]["presentedFrames"] = 1
            errs = evaluate_chromium_ownership_receipt(self.contract, broken, evidence_base_dir=tmp)
            self.assertTrue(any("sandbox must remain enabled" in e for e in errs), errs)
            self.assertTrue(any("did not fail visibly" in e for e in errs), errs)

    def test_doppler_receipt_producer_enforces_sandbox_exact_tokens_and_zero_orphans_on_failed_launch(self) -> None:
        producer = _load_doppler_producer(self.repo_root)
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            diag_path = tmp / "failed-launch-diag.json"
            diag = producer.run_selftest_failed_launch(self.repo_root, diag_path)
            self.assertTrue(diag["launchOutcome"]["timedOut"])
            self.assertTrue(diag["launchOutcome"]["zeroOrphansVerified"])
            self.assertEqual(diag["launchOutcome"]["survivingOrphanPids"], [])

            fake_lib = tmp / "libwebgpu_doe.so"
            fake_lib.write_bytes(b"doe-binary-fixture")
            fake_sha = _sha256_bytes(b"doe-binary-fixture")
            with self.assertRaises(ValueError):
                producer.build_doppler_producer_metadata(
                    backend_id="amd-vulkan",
                    browser_args=["--no-sandbox"],
                    doe_library_path=fake_lib,
                    observed_loaded_libraries=[{"path": str(fake_lib), "sha256": fake_sha}],
                )


if __name__ == "__main__":
    unittest.main()
