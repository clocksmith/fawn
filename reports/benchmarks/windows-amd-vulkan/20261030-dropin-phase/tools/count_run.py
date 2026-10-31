# count_run.py <out jsonl rel home> provider=exe ... : counting-layer runs (1 and 33 iterations) per workload, under the GPU lock
from pathlib import Path
import sys,subprocess,os,time,json
h=Path.home();lock=h/'doe-gpu-lock';out=h/sys.argv[1]
sys.path.insert(0,str(h/'wvk/src-cb7e56ecd'))
from bench.tools.dropin_phase_cohort import harness_args, WORKLOADS
provs=dict(a.split('=',1) for a in sys.argv[2:])
while True:
    try: os.mkdir(lock);break
    except FileExistsError: time.sleep(2)
try:
    for wl in WORKLOADS:
        for name,exe in provs.items():
            for it in (1,33):
                tmp=h/'wvk/count/tmp.json'
                if tmp.exists(): tmp.unlink()
                env=dict(os.environ,VK_ADD_LAYER_PATH=str(h/'wvk/count'),VK_INSTANCE_LAYERS='VK_LAYER_DOE_count',COUNT_LAYER_OUT=str(tmp))
                r=subprocess.run([str(h/exe),*harness_args(h/'wvk/inputs',wl,it)],capture_output=True,text=True,env=env,timeout=300)
                ok=r.returncode==0 and '"status":"ok"' in r.stdout
                counts=[json.loads(l) for l in tmp.read_text().splitlines()] if tmp.exists() else []
                with out.open('a') as f: f.write(json.dumps({'workload':wl,'provider':name,'iterations':it,'ok':ok,'devices':counts,'stderr':r.stderr[-500:]})+'\n')
                with (h/'wvk/count/progress.txt').open('a') as f: f.write(f'{wl} {name} {it} {ok} {len(counts)}\n')
finally:
    os.rmdir(lock)
    with (h/'wvk/count/progress.txt').open('a') as f: f.write('DONE\n')
