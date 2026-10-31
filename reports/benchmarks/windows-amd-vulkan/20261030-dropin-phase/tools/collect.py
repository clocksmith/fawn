"""Copy pulled windows into the report, sanitized, and summarize valid attempts.

collect.py <pulled runs dir> <report dir> <exp>...
"""
import gzip, json, re, shutil, subprocess, sys
from pathlib import Path
src, rep = Path(sys.argv[1]), Path(sys.argv[2])
HOME = re.compile(r'C:(?:\\\\|\\|/)Users(?:\\\\|\\|/)[^\\/"]+')  # any C:\Users\<account> spelling
def clean(text: str) -> str:
    text = HOME.sub('~', text)
    assert not HOME.search(text), 'unsanitized host path'
    return text
for exp in sys.argv[3:]:
    valid = []
    for wl_dir in sorted((src / exp).iterdir()):
        for att in sorted(wl_dir.iterdir()):
            out = rep / exp / wl_dir.name / att.name; out.mkdir(parents=True, exist_ok=True)
            for name in ['gate.json', 'validity.json', 'lock.json', 'cohort.log']:
                if (att / name).exists(): (out / name).write_text(clean((att / name).read_text()))
            for name in ['raw.jsonl', 'gpu-samples.jsonl']:
                if (att / name).exists():
                    with gzip.open(out / (name + '.gz'), 'wt') as f: f.write(clean((att / name).read_text()))
            v = json.loads((att / 'validity.json').read_text()) if (att / 'validity.json').exists() else {}
            if v.get('valid'): valid.append(out / 'raw.jsonl.gz')
    raws = []
    for g in valid:
        plain = g.with_suffix('')  # summarize needs plain jsonl; keep it only transiently
        plain.write_text(gzip.open(g, 'rt').read()); raws.append(plain)
    for control in ['dawn', 'main']:
        if control == 'main' and exp == 'baseline': continue
        args = [sys.executable, '-m', 'bench.tools.dropin_phase_cohort', 'summarize', '--control', control,
                '--out', str((rep / exp / f'summary-vs-{control}.json').resolve())]
        for r in raws: args += ['--raw', str(r.relative_to(rep))]
        subprocess.run(args, cwd=rep, check=True, capture_output=True, env={'PYTHONPATH': str(Path(__file__).resolve().parents[1] / 'winvk'), 'PATH': '/usr/bin:/bin'})
    for r in raws: r.unlink()
    print(exp, 'valid windows', len(valid))
