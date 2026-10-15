"""Complete-generation v2 campaign tooling: frozen schedule and baseline admission on synthetic receipts."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
HARNESS = REPO_ROOT / 'bench/external-projects/doppler-generation'
CONTRACT = HARNESS / 'complete-generation-v2.json'
TEST_BOOTSTRAP_SAMPLES = 400


def load(name: str):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), HARNESS / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RUNNER = load('run-campaign')
ANALYZER = load('analyze-campaign')


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_campaign(root: Path, *, slow_label_b: float = 1.0, foreign_activity: bool = False) -> Path:
    """Synthetic receipts shaped like the worker's campaign output; timing noise is seeded."""
    contract = json.loads(CONTRACT.read_text())
    contract['latencyCampaign']['bootstrapSamples'] = TEST_BOOTSTRAP_SAMPLES
    contract_path = root / 'contract.json'
    contract_path.write_text(json.dumps(contract))
    identity = {'contractSha256': digest(contract_path), 'providerManifestSha256': 'p' * 64, 'referenceSha256': 'r' * 64}
    for provider in RUNNER.PROVIDERS:
        (root / f'{provider}-qualification.json').write_text(json.dumps({'passed': True, **identity}))
    rng = np.random.default_rng(7)
    base = {'dawn': 1000.0, 'doe': 900.0}
    schedule = RUNNER.schedule(contract)
    records = []
    for sample in schedule:
        scale = base[sample['provider']] * (slow_label_b if sample['label'] == 'B' else 1.0)
        rows = []
        for case in contract['cases']:
            for label, count in [('initial', 1), ('warmup', 1), ('resident', 2)]:
                for _ in range(count):
                    complete = float(scale * rng.normal(1.0, 0.004))
                    rows.append({'id': case['id'], 'label': label, 'completeMs': complete, 'firstTokenMs': complete / 10})
        receipt = {'mode': 'campaign', 'lane': sample['provider'], 'controlLabel': sample['label'], 'processIndex': sample['block'],
                   'passed': True, 'rows': rows, 'loadMs': 500.0, 'cleanup': {'elapsedMs': 20.0}, 'cpuMs': 2000.0,
                   'peakProcessRssBytes': 600_000_000, **identity}
        path = root / f'campaign-{sample["name"]}.json'
        path.write_text(json.dumps(receipt))
        records.append({**sample, 'receipt': path.name, 'receiptSha256': digest(path), 'exitCode': 0,
                        'activityError': 'Unrelated GPU activity' if foreign_activity and sample['block'] == 3 else None})
    (root / 'campaign.json').write_text(json.dumps({
        'schemaVersion': 1, 'workloadId': contract['workloadId'], **identity,
        'schedule': schedule, 'records': records, 'complete': True}))
    return contract_path


class CampaignScheduleTests(unittest.TestCase):
    def test_schedule_balances_providers_and_labels(self) -> None:
        contract = json.loads(CONTRACT.read_text())
        schedule = RUNNER.schedule(contract)
        per_label = contract['latencyCampaign']['independentProcessesPerLabel']
        for provider in RUNNER.PROVIDERS:
            for label in contract['latencyCampaign']['controlLabels']:
                self.assertEqual(sum(1 for s in schedule if (s['provider'], s['label']) == (provider, label)), per_label)
        firsts = [next(s['provider'] for s in schedule if s['block'] == block) for block in range(per_label)]
        self.assertEqual(firsts.count('dawn'), firsts.count('doe'))


class CampaignAdmissionTests(unittest.TestCase):
    def analyze(self, **options) -> dict:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract_path = write_campaign(root, **options)
            return ANALYZER.analyze(root, contract_path)

    def test_identical_bytes_on_a_reserved_gpu_make_a_valid_baseline(self) -> None:
        summary = self.analyze()
        self.assertTrue(summary['baselineValid'], [c for c in summary['checks'] if not c['passed']])
        self.assertFalse(summary['performancePromoted'])
        self.assertEqual(summary['claimStatus'], 'not-claimed')
        memory = next(c for c in summary['checks'] if c['id'] == 'requested-and-retained-memory')
        self.assertFalse(memory['passed'], 'superiority needs the separate memory cohort')
        self.assertFalse(summary['superiorityCriteriaPassed'])

    def test_unstable_controls_reject_the_baseline(self) -> None:
        summary = self.analyze(slow_label_b=1.10)
        self.assertFalse(summary['baselineValid'])
        failed = {c['id'] for c in summary['checks'] if not c['passed']}
        self.assertIn('doe-aa-aggregate', failed)
        self.assertFalse(summary['performancePromoted'])

    def test_foreign_gpu_activity_rejects_the_baseline(self) -> None:
        summary = self.analyze(foreign_activity=True)
        self.assertFalse(summary['baselineValid'])
        self.assertIsNone(summary['controls'], 'timing is not analyzed once the cohort is ineligible')


if __name__ == '__main__':
    unittest.main()
