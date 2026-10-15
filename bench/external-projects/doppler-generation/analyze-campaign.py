"""Admit a complete-generation v2 campaign as a valid baseline, or reject it; never promote performance.

One process is one independent block. Each case contributes its resident median per
process; quantiles are linear-interpolated and bootstrap resamples whole processes,
never tokens. A/A stability compares identical-byte labels per provider and per case.
Superiority criteria are computed against the frozen limits but are diagnostic: this
tool sets no claim and no promotion.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

PROVIDERS = ('dawn', 'doe')
RESIDENT = 'resident'
INITIAL = 'initial'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def geomean(values: list[float]) -> float:
    return float(np.exp(np.mean(np.log(values))))


def summarize_process(receipt: dict, case_ids: list[str]) -> dict:
    """Per-case initial, resident-median and first-token values for one process."""
    cases = {}
    for case_id in case_ids:
        rows = [row for row in receipt['rows'] if row.get('id') == case_id]
        resident = [row for row in rows if row['label'] == RESIDENT]
        initial = [row for row in rows if row['label'] == INITIAL]
        if not resident or len(initial) != 1:
            raise ValueError(f'process {receipt.get("processIndex")} lacks {case_id} rows')
        cases[case_id] = {
            'initialMs': initial[0]['completeMs'],
            'residentMs': float(np.median([row['completeMs'] for row in resident])),
            'firstTokenMs': float(np.median([row['firstTokenMs'] for row in resident])),
        }
    return {'cases': cases, 'openingMs': receipt['loadMs'], 'teardownMs': receipt['cleanup']['elapsedMs'],
            'cpuMs': receipt['cpuMs'], 'peakRssBytes': receipt['peakProcessRssBytes']}


def primary(processes: list[dict], case_ids: list[str]) -> float:
    """Equal-weight geometric mean of per-case medians of resident process medians."""
    return geomean([float(np.median([process['cases'][case]['residentMs'] for process in processes]))
                    for case in case_ids])


def bootstrap_ratio(rng, numerator: list[dict], denominator: list[dict], statistic, samples: int, confidence: float):
    """Ratio statistic(numerator)/statistic(denominator) with independent process resampling."""
    point = statistic(numerator) / statistic(denominator)
    draws = np.empty(samples)
    for index in range(samples):
        top = [numerator[i] for i in rng.integers(0, len(numerator), len(numerator))]
        bottom = [denominator[i] for i in rng.integers(0, len(denominator), len(denominator))]
        draws[index] = statistic(top) / statistic(bottom)
    margin = (1 - confidence) / 2
    lower, upper = np.quantile(draws, [margin, 1 - margin])
    return {'ratio': float(point), 'lower': float(lower), 'upper': float(upper)}


def case_statistic(case_id: str, field: str, quantile: float = 0.5):
    return lambda processes: float(np.quantile([process['cases'][case_id][field] for process in processes], quantile))


def field_statistic(field: str, quantile: float = 0.5):
    return lambda processes: float(np.quantile([process[field] for process in processes], quantile))


def analyze(root: Path, contract_path: Path, memory_report: Path | None = None) -> dict:
    contract = json.loads(contract_path.read_text())
    campaign = contract['latencyCampaign']
    case_ids = [case['id'] for case in contract['cases']]
    record = json.loads((root / 'campaign.json').read_text())
    checks = []

    def check(check_id: str, passed: bool, detail=None, kind: str = 'baseline') -> None:
        checks.append({'id': check_id, 'kind': kind, 'passed': bool(passed), **({'detail': detail} if detail is not None else {})})

    check('campaign-complete', record['complete'])
    check('contract-identity', record['contractSha256'] == digest(contract_path))
    expected = {(sample['provider'], sample['label']) for sample in record['schedule']}
    processes = {provider: {label: [] for label in campaign['controlLabels']} for provider in PROVIDERS}
    for provider in PROVIDERS:
        qualification = json.loads((root / f'{provider}-qualification.json').read_text())
        check(f'{provider}-qualified', qualification['passed'] and qualification['contractSha256'] == record['contractSha256'])
    for entry in record['records']:
        receipt_path = root / entry['receipt']
        receipt = json.loads(receipt_path.read_text())
        bound = (digest(receipt_path) == entry['receiptSha256'] and receipt['mode'] == 'campaign'
                 and receipt['lane'] == entry['provider'] and receipt['controlLabel'] == entry['label']
                 and receipt['contractSha256'] == record['contractSha256']
                 and receipt['providerManifestSha256'] == record['providerManifestSha256']
                 and receipt['referenceSha256'] == record['referenceSha256'])
        check(f'{entry["name"]}-receipt', bound and receipt['passed'] and entry['exitCode'] == 0)
        check(f'{entry["name"]}-reserved-gpu', entry['activityError'] is None, entry['activityError'])
        if bound and receipt['passed']:
            processes[entry['provider']][entry['label']].append(summarize_process(receipt, case_ids))
    for provider, label in sorted(expected):
        count = len(processes[provider][label])
        check(f'{provider}-{label}-processes', count == campaign['independentProcessesPerLabel'], count)
    if not all(entry['passed'] for entry in checks):
        return result(contract_path, record, checks, None, None, None)

    rng = np.random.default_rng(campaign['bootstrapSeed'])
    samples, confidence, band = campaign['bootstrapSamples'], campaign['confidence'], campaign['maximumControlRatio']
    a_label, b_label = campaign['controlLabels']
    controls = {}
    for provider in PROVIDERS:
        a, b = processes[provider][a_label], processes[provider][b_label]
        per_case = {case: bootstrap_ratio(rng, b, a, case_statistic(case, 'residentMs'), samples, confidence) for case in case_ids}
        aggregate = bootstrap_ratio(rng, b, a, lambda group: primary(group, case_ids), samples, confidence)
        controls[provider] = {'cases': per_case, 'aggregate': aggregate}
        for case, interval in [*per_case.items(), ('aggregate', aggregate)]:
            check(f'{provider}-aa-{case}', 1 / band <= interval['lower'] and interval['upper'] <= band, interval)

    pooled = {provider: processes[provider][a_label] + processes[provider][b_label] for provider in PROVIDERS}
    gain = bootstrap_ratio(rng, pooled['dawn'], pooled['doe'], lambda group: primary(group, case_ids), samples, confidence)
    check('doe-over-dawn-gain-lower-bound', gain['lower'] >= campaign['minimumDawnGainRatio'], gain, 'superiority-criterion')
    check('corrected-doe-gain', True, 'not applicable: a baseline campaign has no candidate', 'superiority-criterion')
    regression_bounds = {
        ('initialMs', 0.5): campaign['maximumCaseLatencyRegressionRatio'],
        ('residentMs', 0.5): campaign['maximumCaseLatencyRegressionRatio'],
        ('residentMs', 0.95): campaign['maximumCaseLatencyRegressionRatio'],
        ('firstTokenMs', 0.5): campaign['maximumFirstTokenRegressionRatio'],
    }
    comparisons = {}
    for case in case_ids:
        for (field, quantile), limit in regression_bounds.items():
            statistic = case_statistic(case, field, quantile)
            ratio = statistic(pooled['doe']) / statistic(pooled['dawn'])
            comparisons[f'{case}-{field}-p{int(quantile * 100)}'] = ratio
            check(f'{case}-{field}-p{int(quantile * 100)}-regression', ratio <= limit, ratio, 'superiority-criterion')
    for field, limit in [('openingMs', campaign['maximumOpeningRegressionRatio']),
                         ('teardownMs', campaign['maximumTeardownRegressionRatio']),
                         ('cpuMs', campaign['maximumCpuRegressionRatio'])]:
        ratio = field_statistic(field)(pooled['doe']) / field_statistic(field)(pooled['dawn'])
        comparisons[field] = ratio
        check(f'{field}-regression', ratio <= limit, ratio, 'superiority-criterion')
    rss = max(process['peakRssBytes'] for process in pooled['doe']) / max(process['peakRssBytes'] for process in pooled['dawn'])
    comparisons['peakRssBytes'] = rss
    check('peak-rss-regression', rss <= contract['performance']['maximumMemoryRegressionRatio'], rss, 'superiority-criterion')
    if memory_report:
        memory = json.loads(memory_report.read_text())
        check('requested-and-retained-memory', memory.get('passed') is True, memory_report.name, 'superiority-criterion')
    else:
        check('requested-and-retained-memory', False, 'not evaluated: needs a validated memory cohort report', 'superiority-criterion')
    check('instrumentation-perturbation', True, 'not applicable: campaign processes run without observers', 'superiority-criterion')
    providers = {provider: {'primaryMs': primary(pooled[provider], case_ids),
                            'processes': len(pooled[provider])} for provider in PROVIDERS}
    return result(contract_path, record, checks, controls, providers, {'gain': gain, 'ratios': comparisons})


def result(contract_path: Path, record: dict, checks: list[dict], controls, providers, comparison) -> dict:
    baseline_checks = [entry for entry in checks if entry['kind'] == 'baseline']
    criteria = [entry for entry in checks if entry['kind'] == 'superiority-criterion']
    return {
        'schemaVersion': 1,
        'classification': 'diagnostic',
        'workloadId': record['workloadId'],
        'contractSha256': digest(contract_path),
        'campaignSha256': hashlib.sha256(json.dumps(record, sort_keys=True).encode()).hexdigest(),
        'baselineValid': bool(baseline_checks) and all(entry['passed'] for entry in baseline_checks),
        'superiorityCriteriaPassed': bool(criteria) and all(entry['passed'] for entry in criteria),
        'performancePromoted': False,
        'claimStatus': 'not-claimed',
        'controls': controls,
        'providers': providers,
        'comparison': comparison,
        'checks': checks,
        'scope': 'Receipt analysis only. A valid baseline admits the cohort for attribution and later candidate comparison; '
                 'superiority criteria are diagnostic, and no claim or promotion is made here.',
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--contract', type=Path, required=True)
    parser.add_argument('--memory-report', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    summary = analyze(args.root, args.contract, args.memory_report)
    args.out.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'baselineValid': summary['baselineValid'], 'superiorityCriteriaPassed': summary['superiorityCriteriaPassed'],
                      'failedChecks': [entry['id'] for entry in summary['checks'] if not entry['passed']]}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
