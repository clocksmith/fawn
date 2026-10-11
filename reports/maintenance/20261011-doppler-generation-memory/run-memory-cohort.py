"""Retain complete matched local-memory cohorts; never select favorable retries."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from bench.lib.compute_program_gpu_activity import (  # noqa: E402
    capture_activity, reject_activity,
)


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path: Path) -> dict:
    return json.loads(path.read_text())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('policy', type=Path)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    policy = read(args.policy)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runner = ROOT / 'bench/external-projects/doppler-generation/run-memory.mjs'
    manifests = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    activity_policy = {'gpuActivity': 'reject-observed-linux-drm'}
    records = []

    def run(arm: str, lane: str, name: str, mode: str, admission: Path | None = None) -> dict:
        receipt = output / f'{name}.json'
        cache = output / f'{name}-cache'
        cache.mkdir()
        command = ['node', str(runner), str(args.policy.resolve()), str(manifests[arm]),
                   lane, str(receipt), mode]
        if admission:
            command.append(str(admission))
        env = os.environ | {'XDG_CACHE_HOME': str(cache), 'MESA_SHADER_CACHE_DIR': str(cache / 'mesa')}
        with (output / f'{name}.log').open('w') as log:
            with capture_activity(receipt, activity_policy, digest(args.policy), 'vulkan',
                                  'measure' if mode == 'measurement' else mode):
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=180, check=False)
        record = {'name': name, 'arm': arm, 'lane': lane, 'mode': mode,
                  'receipt': str(receipt), 'receiptSha256': digest(receipt) if receipt.exists() else None,
                  'exitCode': result.returncode, 'command': command, 'activityError': None}
        if mode == 'measurement':
            activity = read(Path(f'{receipt}.gpu-activity.json'))
            try:
                reject_activity(activity['snapshots'], activity['target']['pciDevice'])
            except ValueError as error:
                record['activityError'] = str(error)
        records.append(record)
        (output / 'cohort.json').write_text(json.dumps({
            'schemaVersion': 1, 'policySha256': digest(args.policy), 'driverSha256': digest(Path(__file__)),
            'providerManifests': {arm: digest(path) for arm, path in manifests.items()},
            'records': records, 'complete': False,
        }, indent=2) + '\n')
        print(name, result.returncode, record['activityError'] or '', flush=True)
        return record

    # Safety and unchanged output qualify every exact package before profiling or timing.
    for arm in manifests:
        for lane in ['dawn', 'doe']:
            record = run(arm, lane, f'{arm}-{lane}-qualification', 'qualification')
            if record['exitCode'] or not read(Path(record['receipt']))['passed']:
                raise SystemExit('Qualification failed; retained receipt; measurements not admitted')
    for arm in manifests:
        for lane in ['dawn', 'doe']:
            record = run(arm, lane, f'{arm}-{lane}-diagnostic', 'diagnostic')
            receipt = read(Path(record['receipt']))
            if record['exitCode'] or not receipt['passed'] or receipt['observation']['overflow']:
                raise SystemExit('Memory observation failed; retained receipt')

    # Balanced provider and source order; each run is a new process and cache directory.
    for repetition in range(policy['independentProcesses']):
        lead = policy['sampleOrder'][repetition % len(policy['sampleOrder'])]
        lanes = [lead, 'doe' if lead == 'dawn' else 'dawn']
        arms = ['baseline', 'candidate'] if repetition % 2 == 0 else ['candidate', 'baseline']
        for lane in lanes:
            for arm in arms:
                run(arm, lane, f'{repetition:02d}-{arm}-{lane}', 'measurement',
                    output / f'{arm}-{lane}-qualification.json')
    record = read(output / 'cohort.json')
    record['complete'] = True
    (output / 'cohort.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
