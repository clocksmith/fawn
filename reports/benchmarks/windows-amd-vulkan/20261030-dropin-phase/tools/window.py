"""Gated Windows timing window: GPU lock, cohort run, foreign-load sampler, validity.

window.py <out dir rel home> <src rel home> -- <cohort run args...>
Foreign CPU = (GetSystemTimes busy - window tree CPU) / (wall x logical CPUs), where
the window tree is this process, the cohort process and every harness child
(childCpuMs). Foreign GPU = mean over 1 s samples of the max, across engines, of
summed PDH "GPU Engine(*)\\Utilization Percentage" for processes whose image is not
a harness-*.exe and not System (PID 4). System GPU time is WDDM video-memory
paging/zeroing done for every process; it is recorded separately (systemGpuPct). A
15 s pre-window sample under the lock, before any harness starts, must show all
GPU time including System <= 2% and foreign CPU <= 5%, so foreign paging cannot hide
there. Valid iff pre-window and window foreign GPU <= 2%, foreign CPU <= 5%, no
failed process, and the Dawn A/A check passes (checked by the gate script).
"""
from pathlib import Path
import ctypes, ctypes.wintypes as wt, json, os, subprocess, sys, threading, time
h = Path.home(); out = h / sys.argv[1]; src = h / sys.argv[2]; cohort = sys.argv[sys.argv.index('--') + 1:]
out.mkdir(parents=True, exist_ok=False)
k32 = ctypes.WinDLL('kernel32', use_last_error=True); pdh = ctypes.WinDLL('pdh')
def ft(f): return (f.dwHighDateTime << 32) | f.dwLowDateTime
def systimes():
    i, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    k32.GetSystemTimes(ctypes.byref(i), ctypes.byref(k), ctypes.byref(u))
    return ft(i), ft(k), ft(u)
def proctimes(handle):
    t = [wt.FILETIME() for _ in range(4)]
    if not k32.GetProcessTimes(wt.HANDLE(handle), *map(ctypes.byref, t)): raise OSError(ctypes.get_last_error())
    return (ft(t[2]) + ft(t[3])) / 1e4
names = {}
def image(pid):
    if pid not in names:
        hp = k32.OpenProcess(0x1000, False, pid); buf = ctypes.create_unicode_buffer(520); n = wt.DWORD(520)
        names[pid] = buf.value.rsplit('\\', 1)[-1].lower() if hp and k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)) else f'?{pid}'
        if hp: k32.CloseHandle(hp)
    return names[pid]
class ITEM(ctypes.Structure):
    _fields_ = [('name', wt.LPWSTR), ('status', wt.DWORD), ('pad', wt.DWORD), ('value', ctypes.c_double)]
query = wt.HANDLE(); counter = wt.HANDLE()
pdh.PdhOpenQueryW(None, None, ctypes.byref(query))
pdh.PdhAddEnglishCounterW(query, '\\GPU Engine(*)\\Utilization Percentage', None, ctypes.byref(counter))
pdh.PdhCollectQueryData(query)
samples = []; stop = threading.Event()
def sample():
    while not stop.wait(1.0):
        pdh.PdhCollectQueryData(query)
        size = wt.DWORD(0); count = wt.DWORD(0)
        pdh.PdhGetFormattedCounterArrayW(counter, 0x200, ctypes.byref(size), ctypes.byref(count), None)
        buf = (ctypes.c_byte * size.value)()
        if count.value == 0 or pdh.PdhGetFormattedCounterArrayW(counter, 0x200, ctypes.byref(size), ctypes.byref(count), buf) != 0: continue
        items = ctypes.cast(buf, ctypes.POINTER(ITEM)); engines = {}; harness = {}; system = {}; top = {}
        for j in range(count.value):
            name, v = items[j].name, items[j].value
            if v <= 0: continue
            pid = int(name.split('_')[1]); eng = name.split('_luid_')[1]; img = image(pid)
            d = harness if img.startswith('harness-') else system if pid == 4 else engines
            d[eng] = d.get(eng, 0.0) + v
            key = img + '|' + name.rsplit('engtype_', 1)[-1]
            top[key] = top.get(key, 0.0) + v
        samples.append({'t': time.time(), 'foreignMaxEngine': max(engines.values(), default=0.0),
                        'harnessMaxEngine': max(harness.values(), default=0.0), 'systemMaxEngine': max(system.values(), default=0.0), 'byImageEngine': dict(sorted(top.items(), key=lambda x: -x[1])[:8])})
lock = h / 'doe-gpu-lock'; waited = time.time()
while True:
    try: os.mkdir(lock); break
    except FileExistsError: time.sleep(2)
try:
    (out / 'lock.json').write_text(json.dumps({'waitedS': time.time() - waited, 'acquired': time.time()}))
    me = k32.GetCurrentProcess(); th = threading.Thread(target=sample, daemon=True); th.start()
    p0 = proctimes(me); q0 = systimes(); time.sleep(15); q1 = systimes(); p1 = proctimes(me)
    pre_samples = list(samples)
    pre = {'wallS': 15, 'allGpuPct': sum(x['foreignMaxEngine'] + x['systemMaxEngine'] for x in pre_samples) / max(1, len(pre_samples)),
           'foreignCpuPct': 100 * (((q1[1] - q0[1]) + (q1[2] - q0[2]) - (q1[0] - q0[0])) / 1e4 - (p1 - p0)) / (15e3 * os.cpu_count())}
    del samples[:]
    cpu0 = proctimes(me); s0 = systimes(); w0 = time.monotonic()
    if cohort[0] == 'IDLE':  # idle probe: same sampler, no harness
        time.sleep(float(cohort[1])); log = 'idle'; cohort_cpu = 0.0; p = subprocess.CompletedProcess([], 0)
    else:
        p = subprocess.Popen([sys.executable, '-m', 'bench.tools.dropin_phase_cohort', 'run', *cohort], cwd=src,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log = p.communicate()[0]; cohort_cpu = proctimes(int(p._handle))
    stop.set(); th.join(); wall = time.monotonic() - w0; s1 = systimes(); cpu1 = proctimes(me)
finally:
    os.rmdir(lock)
(out / 'cohort.log').write_text(log)
rows = [] if cohort[0] == 'IDLE' else [json.loads(l) for l in Path(cohort[cohort.index('--out') + 1]).read_text().splitlines() if l.strip()]
children = sum(r['childCpuMs'] for r in rows)
busy = ((s1[1] - s0[1]) + (s1[2] - s0[2]) - (s1[0] - s0[0])) / 1e4
ncpu = os.cpu_count(); own = (cpu1 - cpu0) + cohort_cpu + children
gate = {'wallS': wall, 'logicalCpus': ncpu, 'systemBusyMs': busy, 'windowTreeCpuMs': own, 'harnessChildCpuMs': children,
        'foreignCpuPct': 100 * (busy - own) / (wall * 1e3 * ncpu), 'gpuSamples': len(samples),
        'foreignGpuPct': sum(s['foreignMaxEngine'] for s in samples) / max(1, len(samples)),
        'harnessGpuPct': sum(s['harnessMaxEngine'] for s in samples) / max(1, len(samples)),
        'systemGpuPct': sum(s['systemMaxEngine'] for s in samples) / max(1, len(samples)), 'preWindow': pre,
        'failedProcesses': sum(1 for r in rows if r['returnCode'] != 0 or r['result'].get('status') != 'ok'),
        'cohortReturnCode': p.returncode, 'processes': len(rows)}
gate['gateValid'] = (gate['foreignGpuPct'] <= 2.0 and gate['foreignCpuPct'] <= 5.0 and gate['failedProcesses'] == 0
                    and pre['allGpuPct'] <= 2.0 and pre['foreignCpuPct'] <= 5.0)
(out / 'gate.json').write_text(json.dumps(gate, indent=2) + '\n')
(out / 'gpu-samples.jsonl').write_text(''.join(json.dumps(s) + '\n' for s in samples))
print(json.dumps(gate))
