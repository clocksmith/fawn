"""Resume unexecuted M7 API groups after a preserved evaluator overconstraint."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from api_job_m7 import JobApi, preflight_checks, budget_checks, lifecycle_checks

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    report=dict(status='FAIL',checks={},evaluator_correction='Only valid-workspace mutation is permitted on invalid source/output span failure; no-access requirement still enforced for invalid capacities/workspace and ABI/exact-size failure.')
    try:
        api=JobApi(args.library)
        for name,action in [('preflight',lambda:preflight_checks(api)),('budgets',lambda:budget_checks(args.library)),('lifecycle',lambda:lifecycle_checks(args.library))]:
            report['checks'][name]=action()
            (args.out/'resume.json').write_text(json.dumps(report,indent=2)+'\n')
        api.close()
        report['status']='PASS'
    except Exception as error:
        report['first_failure']=str(error)
        print('FAIL:',error)
    (args.out/'resume.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(report['status']!='PASS')

if __name__=='__main__':
    raise SystemExit(main())
