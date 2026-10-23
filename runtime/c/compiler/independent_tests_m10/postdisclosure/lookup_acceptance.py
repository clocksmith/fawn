#!/usr/bin/env python3
"""Turn the preserved diagnostic collector into a failing acceptance check."""
from pathlib import Path
import json,runpy,sys
runpy.run_path(str(Path(__file__).with_name('lookup_regressions_final.py')),run_name='__main__')
p=Path(sys.argv[sys.argv.index('--receipt')+1]);r=json.loads(p.read_text())
assert all(m['passed'] for c in r['cases'] for m in c['modes']),r
