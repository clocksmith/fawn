"""Read-only verification of the compact completed M11 evidence; no test replay."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parent

def read(name):
    return json.loads((ROOT / name).read_text())

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def manifest(name, base, expected=None):
    path = ROOT / name
    if expected:
        assert digest(path) == expected, name
    rows = []
    for line in path.read_text().splitlines():
        sha, relative = line.split(None, 1)
        relative = relative.lstrip('*')
        target = (base / relative).resolve()
        assert base.resolve() in target.parents, relative
        assert digest(target) == sha, relative
        rows.append(relative)
    assert rows and len(set(rows)) == len(rows), name
    return len(rows)

def main():
    seals = {
        'CASE_SEAL.sha256': (ROOT, '9a03dd3f38641834f59a70e6f09a1524a5271daeb1886651ff7575b7a0a2d664'),
        'PRE_CANDIDATE_COMPLETE_V2.sha256': (ROOT, '4d5ac2a45fbaba470387b50626149fd69ee2568eece1a24b803d90b6b25610a6'),
        'native/NATIVE_ARTIFACTS.sha256': (ROOT / 'native', 'ecee22fb466e5d026538bb7c667446b17f3a18073d38db0d4a9a63e400de1601'),
        'native/PORTABLE_NATIVE_FILES.sha256': (ROOT / 'native', '17d70d72fe0ac8ef67902582279eceb3b7ff2c740b30c566d28760ab72730f5b'),
    }
    verified = {name: manifest(name, base, sha) for name, (base, sha) in seals.items()}
    verified['IMPORT.sha256'] = manifest('IMPORT.sha256', ROOT)
    v = read('VERDICT.json')
    assert v['status'] == 'PASS_WITH_PRESERVED_EVALUATOR_CORRECTIONS'
    assert not v['candidate_defects_observed'] and not v['unqualified_required_gates']
    result = read('candidate2-observations/SOURCE_BUILT_CASES_FINAL.json')
    assert len(result['cases']) == 120 and not result['failed'] and not result['pending']
    assert all(c['outcome'] == 'passed' for c in result['cases'])
    assert len({c['id'] for c in result['cases']}) == 120
    assert result['library_sha256'] == '5d301e813223c9602f7c7ad57634b0ad53a68ba8b752066fbfdce1e9f29b2553'
    scenarios = [c for c in result['cases'] if 'execution' in c]
    dispatches = [d for c in scenarios for d in c['execution']]
    stats = dict(execution_scenarios=len(scenarios), dispatches=len(dispatches),
                 successful_dispatches=sum('steps' in d for d in dispatches),
                 expected_race_failures=sum('expected_race_detected' in d for d in dispatches),
                 instructions=sum(d.get('steps', 0) for d in dispatches),
                 events=sum(d.get('events', 0) for d in dispatches),
                 memory_accesses=sum(d.get('memory_accesses', 0) for d in dispatches))
    assert stats == v['execution']
    long = next(c for c in scenarios if c['id'] == 'witness_nonatomic_100')
    assert long == v['long_case'] and len(long['execution']) == 8
    assert all(d['steps'] == 435226078 and d['groups'] == 2 for d in long['execution'])
    corpus = read('candidate2-observations/SAME98_FINAL.json')
    assert not corpus['failed']
    counts = {key: sum(test(row) for row in corpus['records']) for key, test in {
        'paths': lambda r: 1, 'emitted': lambda r: r['emitted'],
        'validated': lambda r: r['validated'], 'executed': lambda r: r['executed'],
        'rejected': lambda r: not r['emitted']}.items()}
    assert counts == corpus['counts'] == v['same98'] == dict(paths=98, emitted=52, validated=52, executed=43, rejected=46)
    assert [r['source'] for r in corpus['records'] if r['validated'] and not r['executed']] == v['new_validated_only']
    assert len(v['new_validated_only']) == 9
    n = read('native/NATIVE_VERDICT.json')
    assert n['status'] == v['status'] and not n['native_gates_unqualified'] and not n['candidate_defects_observed']
    original = read('candidate2-observations/M10_REGRESSION_ORIGINAL.json')
    assert len(original['cases']) == 576 and original['failed'] == ['private']
    assert v['retained_M10_expectation_accounting'] == dict(unchanged_accepted_M10_expectations=574, presealed_workgroup_admission_lift=1, exact_contract_private_status_mapping=1)
    run = read('RUN_INVOCATION.json')
    assert run['exit_code'] == 0 and run['unchanged_run'] and run['wall_clock_timeout'] is None
    assert run['runner_sha256'] == digest(ROOT / 'run_cases.py')
    print(json.dumps(dict(status='VERIFIED_PRESERVED_EVIDENCE_ONLY', manifests=verified, execution=stats, same98=counts), indent=2))

if __name__ == '__main__':
    main()
