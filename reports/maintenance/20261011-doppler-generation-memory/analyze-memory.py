"""Diagnostic process-paired memory analysis; failed controls prevent promotion."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np


def read(path: Path) -> dict:
    return json.loads(path.read_text())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('policy', type=Path)
    parser.add_argument('cohort', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    policy = read(args.policy)
    cohort = read(args.cohort / 'cohort.json')
    settings = read(Path(__file__).with_name('analysis.json'))
    rng = np.random.default_rng(settings['seed'])
    policy_hash = hashlib.sha256(args.policy.read_bytes()).hexdigest()
    assert cohort['policySha256'] == policy_hash and cohort['complete']
    receipts = {}
    for record in cohort['records']:
        path = Path(record['receipt'])
        assert hashlib.sha256(path.read_bytes()).hexdigest() == record['receiptSha256']
        receipt = read(path)
        assert receipt['policySha256'] == policy_hash
        assert receipt['providerManifestSha256'] == cohort['providerManifests'][record['arm']]
        assert record['exitCode'] == 0 and receipt['passed']
        receipts[record['name']] = receipt

    def stats(values: list[float]) -> dict:
        return {'p50': float(np.median(values)), 'p95': float(np.quantile(values, .95)),
                'minimum': min(values), 'maximum': max(values), 'samples': values}

    def paired(left: list[float], right: list[float]) -> dict:
        logs = [math.log(a / b) for a, b in zip(left, right, strict=True)]
        draws = rng.choice(logs, (settings['bootstrapSamples'], len(logs)), replace=True)
        margin = (1 - settings['confidence']) / 2
        bounds = np.quantile(np.exp(np.mean(draws, axis=1)), [margin, 1 - margin])
        return {'ratio': math.exp(float(np.mean(logs))), 'lower': float(bounds[0]),
                'upper': float(bounds[1]), 'pairedLogRatios': logs}

    results = {}
    for lane in ['dawn', 'doe']:
        arms = {}
        for arm in ['baseline', 'candidate']:
            rows = [receipts[f'{i:02d}-{arm}-{lane}'] for i in range(policy['independentProcesses'])]
            diagnostic = receipts[f'{arm}-{lane}-diagnostic']
            assert diagnostic['observation']['overflow'] is False
            assert diagnostic['observation']['snapshots'][-1]['requestedBufferBytes'] == 0
            arms[arm] = {
                'peakProcessRssBytes': stats([r['peakProcessRssBytes'] for r in rows]),
                'loadMs': stats([r['loadMs'] for r in rows]),
                'initial': {field: stats([r['rows'][0][field] for r in rows])
                            for field in ['firstTokenMs', 'completeMs']},
                'resident': {field: stats([x[field] for r in rows for x in r['rows']
                                          if x['phase'].startswith('resident-')])
                             for field in ['firstTokenMs', 'completeMs']},
                'diagnostic': {'maxima': diagnostic['observation']['maxima'],
                               'poolCheckpoints': [c for c in diagnostic['checkpoints'] if c['pools']]},
            }
        memory = paired(arms['baseline']['peakProcessRssBytes']['samples'],
                        arms['candidate']['peakProcessRssBytes']['samples'])
        resident_complete = paired(
            [float(np.mean(arms['candidate']['resident']['completeMs']['samples'][i:i+policy['residentRuns']]))
             for i in range(0, policy['independentProcesses'] * policy['residentRuns'], policy['residentRuns'])],
            [float(np.mean(arms['baseline']['resident']['completeMs']['samples'][i:i+policy['residentRuns']]))
             for i in range(0, policy['independentProcesses'] * policy['residentRuns'], policy['residentRuns'])])
        initial_latency = {field: paired(arms['candidate']['initial'][field]['samples'],
                                         arms['baseline']['initial'][field]['samples'])
                           for field in ['firstTokenMs', 'completeMs']}
        load_latency = paired(arms['candidate']['loadMs']['samples'], arms['baseline']['loadMs']['samples'])
        resident_first_token = paired(
            [float(np.mean(arms['candidate']['resident']['firstTokenMs']['samples'][i:i+policy['residentRuns']]))
             for i in range(0, policy['independentProcesses'] * policy['residentRuns'], policy['residentRuns'])],
            [float(np.mean(arms['baseline']['resident']['firstTokenMs']['samples'][i:i+policy['residentRuns']]))
             for i in range(0, policy['independentProcesses'] * policy['residentRuns'], policy['residentRuns'])])
        activity = [record['activityError'] for record in cohort['records']
                    if record['mode'] == 'measurement' and record['lane'] == lane and record['activityError']]
        candidate_rows = [receipts[f'{i:02d}-candidate-{lane}'] for i in range(policy['independentProcesses'])]
        checks = {
            'primaryMemoryGain': memory['lower'] >= policy['minimumMemoryGainRatio'],
            'residentLatencyRegression': resident_complete['upper'] <= policy['maximumLatencyRegressionRatio'],
            'initialLatencyRegression': all(r['upper'] <= policy['maximumLatencyRegressionRatio'] for r in initial_latency.values()),
            'loadLatencyRegression': load_latency['upper'] <= policy['maximumLatencyRegressionRatio'],
            'residentFirstTokenRegression': resident_first_token['upper'] <= policy['maximumLatencyRegressionRatio'],
            'hostBudget': all(r['peakProcessRssBytes'] <= policy['hostMemoryBudgetBytes'] for r in candidate_rows),
            'requestedBufferBudget': arms['candidate']['diagnostic']['maxima']['requestedBufferBytes'] <= policy['requestedBufferBudgetBytes'],
            'requestedBufferRegression': arms['candidate']['diagnostic']['maxima']['requestedBufferBytes'] <=
                policy['maximumAllocationRegressionRatio'] * arms['baseline']['diagnostic']['maxima']['requestedBufferBytes'],
            'absoluteLatency': all(r['loadMs'] <= policy['maximumLoadMs'] and all(
                x['firstTokenMs'] <= policy['maximumFirstTokenMs'] and x['completeMs'] <= policy['maximumGenerationMs']
                for x in r['rows'] if 'completeMs' in x) for r in candidate_rows),
            'noObservedForeignGpuActivity': not activity,
        }
        results[lane] = {'arms': arms, 'baselineToCandidateRssRatio': memory,
                         'candidateToBaselineResidentLatencyRatio': resident_complete,
                         'candidateToBaselineInitialLatencyRatios': initial_latency,
                         'candidateToBaselineLoadLatencyRatio': load_latency,
                         'candidateToBaselineResidentFirstTokenRatio': resident_first_token,
                         'checks': checks, 'activityFailures': activity,
                         'performancePromoted': all(checks.values())}

    # These backend observations do not establish an advantage when controls fail.
    backend_comparison = paired(results['dawn']['arms']['candidate']['peakProcessRssBytes']['samples'],
                                results['doe']['arms']['candidate']['peakProcessRssBytes']['samples'])
    summary = {'schemaVersion': 1, 'classification': 'diagnostic-memory-comparison',
               'policySha256': policy_hash,
               'cohortSha256': hashlib.sha256((args.cohort / 'cohort.json').read_bytes()).hexdigest(),
               'analysisSettingsSha256': hashlib.sha256(Path(__file__).with_name('analysis.json').read_bytes()).hexdigest(),
               'estimator': 'Process-paired geometric ratios; bootstrap confidence on that estimator. Interpolated p50/p95; no excluded processes.',
               'providers': results, 'candidateDawnToDoeRssRatio': backend_comparison,
               'performancePromoted': all(r['performancePromoted'] for r in results.values()),
               'ownershipRepairProven': all(receipts[f'candidate-{lane}-diagnostic']['checkpoints'][-2]['pools'][0]['resources']['active']['count'] ==
                                           receipts[f'candidate-{lane}-diagnostic']['checkpoints'][1]['pools'][0]['resources']['active']['count']
                                           for lane in ['dawn', 'doe'])}
    args.output.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({lane: result['checks'] for lane, result in results.items()}))


if __name__ == '__main__':
    main()
