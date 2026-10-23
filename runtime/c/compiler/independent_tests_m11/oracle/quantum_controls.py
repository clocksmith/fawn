"""Cross-check original controls at exact instruction scheduling granularity."""
from __future__ import annotations
import argparse
from functools import partial
from pathlib import Path
import sys
import self_controls
import phase_controls


def main():
    p=argparse.ArgumentParser();p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
    for module in (self_controls,phase_controls):
        module.dispatch=partial(module.dispatch,pure_quantum=1)
        sys.argv=[module.__name__,'--validator',str(a.validator),'--receipt',str(a.out/(module.__name__+'.json'))]
        module.main()
    return 0
if __name__=='__main__':raise SystemExit(main())
