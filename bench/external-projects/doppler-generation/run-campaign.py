"""Run the frozen complete-generation v2 campaign; retain every process and never retry.

The schedule follows the contract: per block, providers alternate by the frozen sample
order and identical-byte control labels by the frozen control order, until each
provider and label has its fixed independent processes. Each sample is a fresh process
with empty cache directories. The campaign starts only on a reserved GPU and observes
foreign DRM activity around every process. It produces receipts, not claims.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from bench.lib.compute_program_gpu_activity import (  # noqa: E402
    capture_activity, detect_target, read_snapshot, reject_activity,
)

WORKER = ROOT / 'bench/external-projects/doppler-generation/run-generation.mjs'
ACTIVITY_POLICY = {'gpuActivity': 'reject-observed-linux-drm'}
PROCESS_TIMEOUT_S = 900
PROVIDERS = ('dawn', 'doe')


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path: Path) -> dict:
    return json.loads(path.read_text())


def schedule(contract: dict) -> list[dict]:
    """Frozen balanced order: blocks alternate provider order and control-label order."""
    campaign = contract['latencyCampaign']
    sample_order = contract['performance']['sampleOrder']
    control_order = campaign['controlOrder']
    half = len(sample_order) // 2
    label_half = len(control_order) // 2
    samples = []
    for block in range(campaign['independentProcessesPerLabel']):
        providers = sample_order[(block % 2) * half:(block % 2) * half + half]
        labels = control_order[(block % 2) * label_half:(block % 2) * label_half + label_half]
        for provider in providers:
            for label in labels:
                samples.append({'block': block, 'provider': provider, 'label': label,
                                'name': f'{block:02d}-{provider}-{label}'})
    return samples


def preflight_reserved(seconds: float) -> dict:
    """Refuse to start unless no foreign DRM client advances on the target GPU."""
    target = detect_target()
    before = read_snapshot(target)
    time.sleep(seconds)
    after = read_snapshot(target)
    reject_activity([before, after], target['pciDevice'])
    return {'target': target, 'windowSeconds': seconds}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('contract', type=Path)
    parser.add_argument('providers', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path, help='directory holding doe- and dawn-qualification.json')
    parser.add_argument('--dry-run', action='store_true', help='print the frozen schedule and exit')
    parser.add_argument('--preflight-seconds', type=float, default=10.0)
    args = parser.parse_args()
    contract = read(args.contract)
    if 'latencyCampaign' not in contract:
        raise SystemExit('Campaign runner requires a complete-generation v2 contract')
    samples = schedule(contract)
    if args.dry_run:
        print(json.dumps(samples, indent=2))
        return 0
    output = args.output.resolve()
    for provider in PROVIDERS:
        receipt = output / f'{provider}-qualification.json'
        if not receipt.exists() or not read(receipt)['passed']:
            raise SystemExit(f'{receipt} is missing or failed; qualify both providers before timing')
    try:
        reserved = preflight_reserved(args.preflight_seconds)
    except ValueError as error:
        raise SystemExit(f'GPU is not reserved, campaign not started: {error}') from error
    identity = {
        'contractSha256': digest(args.contract), 'providerManifestSha256': digest(args.providers),
        'referenceSha256': digest(args.reference), 'runnerSha256': digest(Path(__file__)),
        'workerSha256': digest(WORKER),
    }
    campaign_path = output / 'campaign.json'
    records = []

    def save(complete: bool) -> None:
        campaign_path.write_text(json.dumps({
            'schemaVersion': 1, 'workloadId': contract['workloadId'], 'campaignVersion': contract['latencyCampaign']['version'],
            'classification': 'receipts-only', **identity, 'reservedPreflight': reserved,
            'schedule': samples, 'records': records, 'complete': complete,
        }, indent=2) + '\n')

    for sample in samples:
        receipt = output / f'campaign-{sample["name"]}.json'
        cache = output / 'caches' / f'campaign-{sample["name"]}'
        cache.mkdir(parents=True, exist_ok=False)
        env = os.environ | {'XDG_CACHE_HOME': str(cache), 'DOE_PIPELINE_CACHE_DIR': str(cache),
                            'DOE_SHADER_CACHE_DIR': str(cache), 'MESA_SHADER_CACHE_DIR': str(cache)}
        command = ['node', str(WORKER), str(args.contract.resolve()), str(args.providers.resolve()),
                   str(args.reference.resolve()), sample['provider'], str(receipt), 'campaign',
                   str(sample['block']), sample['label']]
        with (output / f'campaign-{sample["name"]}.log').open('w') as log:
            with capture_activity(receipt, ACTIVITY_POLICY, identity['contractSha256'], 'vulkan', 'measure'):
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=PROCESS_TIMEOUT_S, check=False)
        activity_error = None
        try:
            activity = read(Path(f'{receipt}.gpu-activity.json'))
            reject_activity(activity['snapshots'], activity['target']['pciDevice'])
        except (ValueError, OSError) as error:
            activity_error = str(error)
        records.append({**sample, 'receipt': receipt.name, 'receiptSha256': digest(receipt) if receipt.exists() else None,
                        'exitCode': result.returncode, 'activityError': activity_error})
        save(False)
        print(sample['name'], result.returncode, activity_error or '', flush=True)
        if result.returncode != 0:
            raise SystemExit(f'{sample["name"]} failed; campaign stopped, every receipt retained, no timing admitted')
    save(True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
