"""Unit tests for the C2 offline WebGPU graphics, Metal tail, and Chromium ownership gate."""

from __future__ import annotations

import copy
import unittest
from pathlib import Path
from typing import Any

from bench.gates.webgpu_graphics_campaign_gate import (
    CONTRACT_REL_PATH,
    evaluate_backend_campaign_receipt,
    evaluate_chromium_ownership_receipt,
    resolve_repo_root,
    validate_campaign_contract,
)
from bench.lib.bench_utils import load_json_object


def _valid_workload_rows() -> list[dict[str, Any]]:
    base_resources = {
        "cpuTimeMs": 120.0,
        "processRssBytes": 420_000_000,
        "gpuAllocatedBytes": 180_000_000,
        "failOrSkipCount": 0,
    }
    gfx_phases = {
        "coldSetupMs": 95.0,
        "commandPreparationMs": 1.1,
        "cpuVisibleCompletionMs": 4.2,
        "displayedPresentationMs": 5.8,
    }
    return [
        {
            "workloadId": "doppler_complete_generation",
            "unchangedApplication": True,
            "substitutedMicrobenchmark": False,
            "oraclePassed": True,
            "timedSamples": 9,
            "dawnP50Ms": 1400.0,
            "doeP50Ms": 1080.0,
            "dawnP95Ms": 1520.0,
            "doeP95Ms": 1180.0,
            "timingPhasesMs": {
                "coldSetupMs": 310.0,
                "commandPreparationMs": 12.0,
                "cpuVisibleCompletionMs": 1080.0,
            },
            "resources": copy.deepcopy(base_resources),
        },
        {
            "workloadId": "three_webgpu_fog_scene",
            "unchangedApplication": True,
            "substitutedMicrobenchmark": False,
            "oraclePassed": True,
            "displayedCanvasVerified": True,
            "timingMethod": "displayed_canvas_complete_frame",
            "timedSamples": 15,
            "dawnP50Ms": 14.2,
            "doeP50Ms": 12.1,
            "dawnP95Ms": 16.5,
            "doeP95Ms": 14.4,
            "timingPhasesMs": copy.deepcopy(gfx_phases),
            "resources": copy.deepcopy(base_resources),
        },
        {
            "workloadId": "image_filter_pipeline_2d",
            "unchangedApplication": True,
            "substitutedMicrobenchmark": False,
            "oraclePassed": True,
            "displayedCanvasVerified": True,
            "timingMethod": "displayed_canvas_complete_frame",
            "timedSamples": 15,
            "dawnP50Ms": 8.8,
            "doeP50Ms": 7.6,
            "dawnP95Ms": 9.9,
            "doeP95Ms": 8.6,
            "timingPhasesMs": copy.deepcopy(gfx_phases),
            "resources": copy.deepcopy(base_resources),
        },
        {
            "workloadId": "particles_fluids_simulation",
            "unchangedApplication": True,
            "substitutedMicrobenchmark": False,
            "oraclePassed": True,
            "displayedCanvasVerified": True,
            "timingMethod": "displayed_canvas_complete_frame",
            "timedSamples": 15,
            "dawnP50Ms": 11.5,
            "doeP50Ms": 9.9,
            "dawnP95Ms": 13.0,
            "doeP95Ms": 11.2,
            "timingPhasesMs": copy.deepcopy(gfx_phases),
            "resources": copy.deepcopy(base_resources),
        },
        {
            "workloadId": "texture_upload_canvas_presentation",
            "unchangedApplication": True,
            "substitutedMicrobenchmark": False,
            "oraclePassed": True,
            "displayedCanvasVerified": True,
            "timingMethod": "displayed_canvas_complete_frame",
            "timedSamples": 15,
            "dawnP50Ms": 6.6,
            "doeP50Ms": 5.5,
            "dawnP95Ms": 7.5,
            "doeP95Ms": 6.4,
            "timingPhasesMs": copy.deepcopy(gfx_phases),
            "resources": copy.deepcopy(base_resources),
        },
    ]


class WebGpuGraphicsCampaignGateTests(unittest.TestCase):
    """Verify C2 offline contract integrity and fail-closed receipt checks."""

    def setUp(self) -> None:
        self.root: Path = resolve_repo_root()
        self.contract = load_json_object(self.root / CONTRACT_REL_PATH)
        self.thresholds = load_json_object(
            self.root / "config/benchmark-methodology-thresholds.json"
        )

    def test_repository_contract_is_valid(self) -> None:
        self.assertEqual(validate_campaign_contract(self.root), [])

    def test_valid_vulkan_campaign_passes(self) -> None:
        receipt = {
            "backendId": "amd-vulkan",
            "pooledAcrossBackends": False,
            "sourceMatchedDawn": True,
            "identicalValidationCacheAndWarmup": True,
            "fallbackOccurred": False,
            "aaIndependentRuns": 3,
            "aaP50DriftRatio": 1.02,
            "workloadRows": _valid_workload_rows(),
            "tailObligations": [],
        }
        self.assertEqual(
            evaluate_backend_campaign_receipt(self.contract, self.thresholds, receipt),
            [],
        )

    def test_rejects_raf_substitute_and_missing_oracle(self) -> None:
        rows = _valid_workload_rows()
        rows[1]["timingMethod"] = "request_animation_frame_interval_only"
        rows[1]["displayedCanvasVerified"] = False
        rows[0]["oraclePassed"] = False
        receipt = {
            "backendId": "amd-vulkan",
            "sourceMatchedDawn": True,
            "identicalValidationCacheAndWarmup": True,
            "aaIndependentRuns": 2,
            "aaP50DriftRatio": 1.01,
            "workloadRows": rows,
        }
        errors = evaluate_backend_campaign_receipt(
            self.contract, self.thresholds, receipt
        )
        self.assertTrue(any("independent correctness oracle" in e for e in errors))
        self.assertTrue(any("displayed canvas presentation" in e for e in errors))
        self.assertTrue(any("forbidden timing substitute" in e for e in errors))

    def test_rejects_unaudited_suspicious_speedup(self) -> None:
        rows = _valid_workload_rows()
        rows[4]["dawnP50Ms"] = 66.0
        rows[4]["doeP50Ms"] = 5.0  # 13.2x >= 10.0x suspiciousSpeedupRatio
        receipt = {
            "backendId": "amd-vulkan",
            "sourceMatchedDawn": True,
            "identicalValidationCacheAndWarmup": True,
            "aaIndependentRuns": 2,
            "aaP50DriftRatio": 1.02,
            "workloadRows": rows,
        }
        errors = evaluate_backend_campaign_receipt(
            self.contract, self.thresholds, receipt
        )
        self.assertTrue(any("fairnessAuditPassed" in e for e in errors))

    def test_metal_requires_frozen_vulkan_and_retained_onnx_tail(self) -> None:
        bad_metal = {
            "backendId": "apple-metal",
            "priorBackendCheckpointFrozen": False,
            "pooledAcrossBackends": True,
            "sourceMatchedDawn": True,
            "identicalValidationCacheAndWarmup": True,
            "aaIndependentRuns": 2,
            "aaP50DriftRatio": 1.02,
            "workloadRows": _valid_workload_rows(),
            "tailObligations": [],
        }
        errors = evaluate_backend_campaign_receipt(
            self.contract, self.thresholds, bad_metal
        )
        self.assertTrue(any("cross-backend pooling" in e for e in errors))
        self.assertTrue(any("amd-vulkan checkpoint first" in e for e in errors))
        self.assertTrue(any("onnx-runtime-metal-tail-p95-p99" in e for e in errors))

        good_metal = {
            "backendId": "apple-metal",
            "priorBackendCheckpointFrozen": True,
            "pooledAcrossBackends": False,
            "sourceMatchedDawn": True,
            "identicalValidationCacheAndWarmup": True,
            "aaIndependentRuns": 3,
            "aaP50DriftRatio": 1.03,
            "workloadRows": _valid_workload_rows(),
            "tailObligations": [
                {
                    "obligationId": "onnx-runtime-metal-tail-p95-p99",
                    "retainedAdverseRun": True,
                    "usedMeanToHideTail": False,
                    "p95Percent": -4.2,
                    "p99Percent": -8.1,
                    "disposition": "unqualified-scope-disclosed",
                }
            ],
        }
        self.assertEqual(
            evaluate_backend_campaign_receipt(
                self.contract, self.thresholds, good_metal
            ),
            [],
        )

    def test_chromium_ownership_requires_disabled_doe_failure(self) -> None:
        failing = {
            "doeOwnedLifecycleStages": ["device_creation", "submission"],
            "disabledDoeFailedVisibly": False,
            "silentDawnFallbackOccurred": True,
            "sandboxEnabled": True,
            "maintainabilityGatesPassed": True,
            "ranThreeWayComparison": True,
        }
        errors = evaluate_chromium_ownership_receipt(self.contract, failing)
        self.assertTrue(any("disabled-Doe negative control" in e for e in errors))
        self.assertTrue(any("silent fallback to Dawn" in e for e in errors))
        self.assertTrue(
            any("three-way comparison ran before page ownership" in e for e in errors)
        )

        passing = {
            "doeOwnedLifecycleStages": [
                "device_creation",
                "shader_compilation",
                "submission",
                "mapped_readback",
                "canvas_presentation",
                "destruction",
            ],
            "disabledDoeFailedVisibly": True,
            "silentDawnFallbackOccurred": False,
            "sandboxEnabled": True,
            "maintainabilityGatesPassed": True,
            "ranThreeWayComparison": False,
        }
        self.assertEqual(
            evaluate_chromium_ownership_receipt(self.contract, passing), []
        )


if __name__ == "__main__":
    unittest.main()
