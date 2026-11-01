# Run from repo with PYTHONPATH=., DOE_METAL_WORK and DOE_METAL_INPUTS set.
# Arguments: workload cohort [first-attempt=1] [last-attempt=2]. Binaries: <work>/bin-<cohort>/{main,cand,dawn,dawn-aa}.
from pathlib import Path
import json,subprocess,sys,time,os
from bench.tools import dropin_phase_cohort as c
R=Path.cwd(); W=Path(os.environ['DOE_METAL_WORK']).resolve(); P=R/'reports/benchmarks/apple-metal/20261030-dropin-phase'; wl=sys.argv[1]; cohort=sys.argv[2] if len(sys.argv)>2 else 'submit-storage'
B = W / ('bin-' + cohort)
if not B.is_dir(): B = W / 'bin'
for attempt in range(int(sys.argv[3]) if len(sys.argv) > 3 else 1, (int(sys.argv[4]) if len(sys.argv) > 4 else 2) + 1):
    # Scheduling only, outside the GPU lock; the frozen gate decides validity.
    # Quiet admission runs inside the shared lock; a busy slot starts no window.
    dest=W/cohort/wl/f'attempt{attempt}'; dest.parent.mkdir(parents=True,exist_ok=True)
    raw=dest.parent/f'attempt{attempt}-raw.jsonl'
    argv=['flock','/tmp/doe-gpu-timing.lock','bash','-c','bash "$1" 60 || exit 75; shift; exec "$@"','metal-window',str(P/'tools/wait_quiet.sh'),'caffeinate','-i',sys.executable,str(P/'tools/mac_window.py'),'--out-dir',str(dest),'--lock',str(W/'timing.lock'),'--','--inputs',os.environ['DOE_METAL_INPUTS']]
    for name in ['main','cand','dawn','dawn-aa']: argv+=['--provider',f'{name}={B}/{name}']
    argv+=['--workload',wl,'--pairs','96','--iterations','32','--out',str(raw)]
    while True:
        with (dest.parent/f'attempt{attempt}-driver.out').open('a') as out:
            r=subprocess.run(argv,stdout=out,stderr=subprocess.STDOUT)
        if r.returncode != 75:
            break
        time.sleep(5)
    if r.returncode==0:
        rows=[json.loads(l) for l in raw.read_text().splitlines()]
        pairs={}
        for row in rows:
            if row['provider'] in ('dawn','dawn-aa'):pairs.setdefault(row['pair'],{})[row['provider']]=c.process_metrics(row)['warm.operation']
        ratios=[p['dawn-aa']/p['dawn'] for p in pairs.values()]
        import statistics
        ci=c.bootstrap_median_ci(ratios);med=statistics.median(ratios)
        valid=.95<=med<=1.05 and ci[0]<=1<=ci[1]
        print(wl,attempt,'gate PASS; aa',med,ci,'valid',valid,flush=True)
        if valid: break
    else: print(wl,attempt,'invalid gate rc',r.returncode,flush=True)
