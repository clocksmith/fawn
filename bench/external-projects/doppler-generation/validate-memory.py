"""Authenticate retained local-memory diagnostics without promoting performance."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from bench.lib.compute_program_gpu_activity import reject_activity  # noqa: E402


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--local-archives', action='store_true')
    args = parser.parse_args()
    report = args.report.resolve()
    schema = json.loads((ROOT / 'config/doppler-generation-memory-evidence.schema.json').read_text())
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    manifest = json.loads((report / 'manifest.json').read_text())
    validator.validate(manifest)
    retained = {}
    for artifact in manifest['artifacts']:
        path = (report / artifact['path']).resolve()
        assert path.is_relative_to(report), 'Artifact escapes report'
        data = path.read_bytes()
        assert len(data) == artifact['bytes'] and digest(data) == artifact['sha256'], path
        payload = gzip.decompress(data) if path.suffix == '.gz' else data
        retained[artifact['path'].removesuffix('.gz')] = payload
        if artifact['schemaDefinition']:
            validator.validate(json.loads(payload))
    policy = json.loads(retained['policy.json'])
    policy_hash = digest(retained['policy.json'])
    assert policy_hash == manifest['policySha256']
    assert digest(retained['reference.json']) == policy['referenceSha256']
    assert digest(retained['generation-contract.json']) == policy['generationContractSha256']
    reference = json.loads(retained['reference.json'])
    expected = next(c for c in reference['cases'] if c['id'] == policy['caseId'])
    summary = json.loads(retained['summary.json'])
    validator.validate(summary)
    assert summary['performancePromoted'] is False and summary['ownershipRepairProven'] is True
    cohort = json.loads(retained['receipts/cohort.json'])
    assert summary['cohortSha256'] == digest(retained['receipts/cohort.json'])
    assert cohort['complete'] and cohort['policySha256'] == policy_hash
    measurements = [r for r in cohort['records'] if r['mode'] == 'measurement']
    assert len(measurements) == policy['independentProcesses'] * 4
    sessions = {}
    for record in cohort['records']:
        raw = retained[f"receipts/{record['name']}.json"]
        assert digest(raw) == record['receiptSha256'] and record['exitCode'] == 0
        receipt = json.loads(raw)
        assert receipt['passed'] and receipt['policySha256'] == policy_hash
        assert receipt['runnerSha256'] == digest(retained['run-memory.mjs'])
        assert receipt['observerSha256'] == digest(retained['observe-memory.mjs'])
        provider_name = f"{record['arm']}-providers.json"
        assert receipt['providerManifestSha256'] == digest(retained[provider_name])
        assert receipt['runtimeSession']['runtime']['session']['kvcache']['maxSeqLen'] == policy['contextCapacityTokens']
        if record['lane'] == 'doe':
            assert receipt['providerIdentity']['doeNative'] is True
            assert receipt['providerIdentity']['doeLibraryPath'] == json.loads(retained[provider_name])['doeLibrary']
        for row in receipt['rows']:
            if row['phase'] == 'cancellation':
                assert row['completionSettled'] and expected['outputText'].startswith(''.join(row['chunks']))
            else:
                assert row['tokens'] == expected['tokenIds'] and row['text'] == expected['outputText']
                assert row['stats']['stopReason'] == 'stop-token'
        assert len(receipt['rows']) == policy['residentRuns'] + policy['warmupRuns'] + 3
        assert receipt['cleanup']['unloaded'] and receipt['cleanup']['destroyedDevices'] > 0
        sessions[record['name']] = receipt
        if record['mode'] == 'measurement':
            admission = retained[f"receipts/{record['arm']}-{record['lane']}-qualification.json"]
            assert receipt['admissionSha256'] == digest(admission)
            activity = json.loads(retained[f"receipts/{record['name']}.json.gpu-activity.json"])
            assert activity['evaluationHash'] == digest(raw) and activity['policyHash'] == policy_hash
            error = None
            try:
                reject_activity(activity['snapshots'], activity['target']['pciDevice'])
            except ValueError as failure:
                error = str(failure)
            assert record['activityError'] == error
    for lane in ['doe', 'dawn']:
        assert sessions[f'baseline-{lane}-qualification']['runtimeSession'] == sessions[f'candidate-{lane}-qualification']['runtimeSession']
        diagnostic = sessions[f'candidate-{lane}-diagnostic']
        assert diagnostic['observation']['overflow'] is False
        assert diagnostic['observation']['snapshots'][-1]['requestedBufferBytes'] == 0
        pool_states = [c['pools'][0] for c in diagnostic['checkpoints'] if c['pools']]
        assert pool_states[0]['resources']['active'] == pool_states[-1]['resources']['active']
        assert pool_states[0]['resources']['retainedModel'] == pool_states[-1]['resources']['retainedModel']
        assert summary['providers'][lane]['checks']['primaryMemoryGain'] is False
        assert summary['providers'][lane]['performancePromoted'] is False
    if args.local_archives:
        for artifact in manifest['archives']:
            path = Path(artifact['path'])
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            assert path.stat().st_size == artifact['bytes'] and actual == artifact['sha256'], path
    print(json.dumps({'passed': True, 'performancePromoted': False, 'artifactsVerified': len(retained)}))


if __name__ == '__main__':
    main()
