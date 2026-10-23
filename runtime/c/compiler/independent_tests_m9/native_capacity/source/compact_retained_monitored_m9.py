"""Measure sampled persisted-byte peak of the unchanged compact native/CLI stage."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import sys
import threading
import time
import compact_retained_m9


def main() -> int:
    output=Path(sys.argv[sys.argv.index('--out')+1]).resolve()
    stop=threading.Event()
    report=dict(sample_period_seconds=0.05,samples=0,peak_output_file_bytes=0,
                initial_filesystem_free_bytes=shutil.disk_usage(output.parent).free,
                minimum_filesystem_free_bytes=shutil.disk_usage(output.parent).free,
                limitation='Sampled persisted files within this output only; free-space observations include other workers and do not prove exact transient compiler peak.')
    def sample() -> None:
        while not stop.is_set():
            size=0
            try:
                for p in output.rglob('*'):
                    try:
                        if p.is_file(): size+=p.stat().st_size
                    except FileNotFoundError: pass
            except FileNotFoundError: pass
            report['samples']+=1
            report['peak_output_file_bytes']=max(report['peak_output_file_bytes'],size)
            report['minimum_filesystem_free_bytes']=min(report['minimum_filesystem_free_bytes'],shutil.disk_usage(output.parent).free)
            stop.wait(0.05)
    thread=threading.Thread(target=sample,daemon=True); thread.start()
    started=time.monotonic()
    try: return compact_retained_m9.main()
    finally:
        stop.set();thread.join();report['elapsed_seconds']=time.monotonic()-started
        output.mkdir(parents=True,exist_ok=True)
        (output/'sampled-disk-usage.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')

if __name__=='__main__': raise SystemExit(main())
