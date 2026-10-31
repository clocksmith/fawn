"""Run every frozen windows-amd-host window: hash check, then gated windows with retries."""
from pathlib import Path
import hashlib, json, subprocess, sys, time
h = Path.home(); wv = h / 'wvk'; log = (wv / 'runs').mkdir(exist_ok=True) or open(wv / 'runs/master.log', 'a', buffering=1)
def say(*a): log.write(time.strftime('%Y-%m-%dT%H:%M:%S ') + ' '.join(map(str, a)) + '\n')
PROV = {'doe': 'doe-main/harness-doe.exe', 'main': 'doe-main/harness-doe.exe', 'dawn': 'dawn-bin/harness-dawn.exe',
        'dawn-aa': 'dawn-bin/harness-dawn-aa.exe', 'm7r': 'doe-m7r/harness-doe-m7r.exe', 'm2r': 'doe-m2r/harness-doe-m2r.exe',
        'combined': 'doe-cdirect/harness-doe-cdirect.exe'}
order = sys.argv[1:] or ['baseline', 'combined', 'm7r', 'm2r']
for exp in order:
    c = json.loads((wv / f'contract-{exp}.json').read_text())
    bad = [p for p, d in c['hashes'].items() if hashlib.sha256((h / p).read_bytes()).hexdigest() != d]
    if bad: say('HASH MISMATCH', exp, bad); sys.exit(1)
    say('hashes ok', exp, len(c['hashes']))
    for wl in c['workloads']:
        for attempt in range(1, 5):
            out = f'wvk/runs/{exp}/{wl}/attempt{attempt}'
            raw = h / out / 'raw.jsonl'
            args = [sys.executable, str(wv / 'window.py'), out, 'wvk/src-cb7e56ecd', '--', '--inputs', str(wv / 'inputs'),
                    *[x for p in c['providers'] for x in ('--provider', f'{p}={wv / PROV[p]}')], '--workload', wl,
                    '--pairs', str(c['sampling']['pairs']), '--iterations', '32', '--out', str(raw)]
            say('start', exp, wl, attempt)
            r = subprocess.run(args, capture_output=True, text=True)
            gate = json.loads((h / out / 'gate.json').read_text()) if (h / out / 'gate.json').exists() else {'gateValid': False}
            s = subprocess.run([sys.executable, '-m', 'bench.tools.dropin_phase_cohort', 'summarize', '--raw', str(raw), '--control', 'dawn',
                                '--out', str(h / out / 'summary-vs-dawn.json')], cwd=wv / 'src-cb7e56ecd', capture_output=True, text=True)
            try:
                aa = json.loads((h / out / 'summary-vs-dawn.json').read_text())['workloads'][wl]['paired']['dawn-aa/dawn']['ratio']['warm.operation']
                aa_ok = 0.95 <= aa['median'] <= 1.05 and aa['ci95'][0] <= 1 <= aa['ci95'][1]
            except Exception as e:
                aa, aa_ok = str(e), False
            valid = gate.get('gateValid') and aa_ok
            (h / out / 'validity.json').write_text(json.dumps({'gate': gate, 'aa': aa, 'aaOk': aa_ok, 'valid': bool(valid), 'stderr': r.stderr[-2000:]}, indent=1))
            say('end', exp, wl, attempt, 'valid' if valid else 'INVALID', 'fcpu %.2f fgpu %.2f pre %s aa %s' % (gate.get('foreignCpuPct', -1), gate.get('foreignGpuPct', -1), gate.get('preWindow'), aa))
            if valid: break
            time.sleep(120)
        else:
            say('EXHAUSTED', exp, wl)
say('MASTER DONE', order)
