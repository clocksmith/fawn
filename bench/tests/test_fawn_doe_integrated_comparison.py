"""Proof 3's frozen contract: pinned inputs, arms bound to the strategy, and a derived spend bound."""

from __future__ import annotations

import copy
import hashlib
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = REPO_ROOT / "config/fawn-doe-integrated-comparison.json"


def load(relative: str) -> dict:
    return json.loads((REPO_ROOT / relative).read_text(encoding="utf-8"))


def sha256(relative: str) -> str:
    return hashlib.sha256((REPO_ROOT / relative).read_bytes()).hexdigest()


class IntegratedComparisonContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.validator = Draft202012Validator(json.loads(CONTRACT_PATH.with_suffix("").with_suffix(".schema.json").read_text(encoding="utf-8")))

    def test_repository_pins_match_their_files(self) -> None:
        workloads = {entry["id"]: entry for entry in self.contract["workloads"]}
        pins = [
            (workloads["agent-suite"]["source"]["policyPath"], workloads["agent-suite"]["source"]["policySha256"]),
            (workloads["doppler-browser-generation"]["source"]["contractPath"], workloads["doppler-browser-generation"]["source"]["contractSha256"]),
            (workloads["doppler-browser-generation"]["oracle"]["referencePath"], workloads["doppler-browser-generation"]["oracle"]["referenceSha256"]),
            (workloads["umap-sgd"]["source"]["inputsPath"], workloads["umap-sgd"]["source"]["inputsSha256"]),
            (workloads["umap-sgd"]["oracle"]["oraclePath"], workloads["umap-sgd"]["oracle"]["oracleSha256"]),
            (workloads["three-fog-scattering"]["source"]["dependencyManifestPath"], workloads["three-fog-scattering"]["source"]["dependencyManifestSha256"]),
        ]
        for path, digest in pins:
            with self.subTest(path=path):
                self.assertEqual(sha256(path), digest, "a frozen proof-3 input changed")

    def test_arms_and_proof_bind_to_the_strategy(self) -> None:
        proof = load("config/doe-product-strategy.json")["roadmapProofs"]["proofs"][2]
        self.assertEqual(proof["id"], self.contract["strategyProof"])
        self.assertEqual(proof["contractPath"], "config/fawn-doe-integrated-comparison.json")
        self.assertEqual([arm["name"] for arm in self.contract["arms"]], proof["arms"])

    def test_spend_bound_follows_from_the_frozen_suite_budgets(self) -> None:
        agent = self.contract["workloads"][0]
        suite = load(agent["source"]["policyPath"])["workspaceComparison"]["modelDrivenDifferentiation"]
        scenarios = len(load("config/browser-product-comparison-policy.json")["workspaceComparison"]["differentiation"]["scenarios"]) \
            + len(load("config/browser-product-comparison-policy.json")["workspaceComparison"]["revalidatedDifferentiation"]["scenarios"]) \
            + sum(len(workflow["scenarios"]) for workflow in load("browser/workspace/qualification/model-agent/held-out-workflows.json")["workflows"].values())
        tasks = scenarios * agent["repetitions"] * len(agent["armRoles"])
        prices = agent["spend"]["pricesPerMillionTokensUsd"]
        per_task = (suite["agent"]["maxInputTokensPerTask"] * prices["input"]
                    + suite["agent"]["maxTurnsPerTask"] * suite["agent"]["maxOutputTokensPerTurn"] * prices["output"]) / 1e6
        self.assertEqual(agent["spend"]["tasks"], tasks)
        self.assertAlmostEqual(agent["spend"]["worstCaseUsd"], round(tasks * per_task, 2), places=2)
        self.assertLessEqual(agent["spend"]["worstCaseUsd"], agent["spend"]["approvedCeilingUsd"])
        self.assertEqual(prices, suite["spend"]["pricesPerMillionTokensUsd"])

    def test_schema_rejects_weakened_contracts(self) -> None:
        self.validator.validate(self.contract)
        for mutate in (
            lambda c: c["arms"].pop(),
            lambda c: c["arms"][2].update(webgpuProvider="dawn"),
            lambda c: c["browserBuilds"]["launch"].update(sandbox="disabled"),
            lambda c: c["chromiumSource"].update(internalDiskAllowed=True),
            lambda c: c["controls"].update(maximumControlRatio=1.2),
            lambda c: c["workloads"][1].update(marginRatio=1.05),
            lambda c: c["decisionRules"].pop(),
            lambda c: c["runPolicy"].update(fullRuns=2),
            lambda c: c["runPolicy"].update(outcomeSelectedRetries=True),
            lambda c: c["sampling"]["gpuWorkloads"].update(processesPerLabel=8),
            lambda c: c["workloads"][3]["oracle"]["capture"].update(maskedPixels="overlay"),
            lambda c: c["workloads"][3]["oracle"]["capture"].pop("animationClock"),
            lambda c: c["workloads"][3]["oracle"]["tolerance"].update(p99AbsoluteDifference=8),
            lambda c: c["workloads"][3].update(marginRatio=1.05),
            lambda c: c["workloads"][0]["spend"].update(approvedCeilingUsd=500),
            lambda c: c["workloads"][0]["spend"].pop("billingPrecondition"),
            lambda c: c.pop("supersedes"),
        ):
            candidate = copy.deepcopy(self.contract)
            mutate(candidate)
            self.assertFalse(self.validator.is_valid(candidate))


if __name__ == "__main__":
    unittest.main()
