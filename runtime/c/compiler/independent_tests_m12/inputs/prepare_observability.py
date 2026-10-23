"""Add read-only progress observation without changing scheduling or target steps."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parent
source=root/'workgroup_m12.py'
s=source.read_text()
s=s.replace('import random\n', 'import random\nimport time\n')
s=s.replace('event_limit=100000000, pure_quantum=64, shared_backing=None):', 'event_limit=100000000, pure_quantum=64, shared_backing=None,\n             progress_callback=None, progress_seconds=20):')
s=s.replace('    sweeps = 0\n', '    sweeps = 0\n    next_report = time.monotonic() + progress_seconds\n')
s=s.replace('        sweeps += 1\n', '''        sweeps += 1
        if progress_callback is not None and time.monotonic() >= next_report:
            suspended_steps = sum(task.gi_frame.f_locals.get('steps', 0)
                                  for key, task in tasks.items()
                                  if key not in finished and task.gi_frame is not None)
            progress_callback({'sweeps': sweeps, 'events': events,
                               'memory_accesses': observer.total,
                               'steps': sum(x['steps'] for x in finished.values()) + suspended_steps,
                               'finished_invocations': len(finished),
                               'total_invocations': len(tasks),
                               'barriers_per_group': {str(k): v for k, v in barrier_counts.items()}})
            next_report = time.monotonic() + progress_seconds
''')
(root/'workgroup_observed.py').write_text(s)
(root/'OBSERVABILITY.json').write_text(json.dumps({'base_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'observed_source_sha256':hashlib.sha256(s.encode()).hexdigest(),'semantics':'Identical scheduler, order, quantum and yields. Optional callback reads counters and generator locals between existing sweeps; it does not resume or mutate an invocation.','progress_seconds':20,'wall_clock_timeout':None,'timeout_policy':'Retain existing 100000000 event bound per dispatch and 500000000 instruction bound per invocation, fail closed as Unfinished. No estimated duration is a timeout. Parent supervises live persisted counters; inspect prolonged lack of counter change rather than claiming progress from process liveness. Task remains active until terminal result or verified blocker.','schedules':['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7'],'dispatches_each':2,'pure_quantum':64},indent=2,sort_keys=True)+'\n')
