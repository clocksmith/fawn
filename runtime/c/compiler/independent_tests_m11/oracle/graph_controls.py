"""Frozen explicit graph reachability and mutation checks, no compiler involved."""
from __future__ import annotations
import json
from pathlib import Path


def reaches(edges,source='R',sink='N'):
    adjacency={}
    for a,b in edges:adjacency.setdefault(a,[]).append(b)
    seen=set();pending=[source]
    while pending:
        node=pending.pop()
        if node==sink:return True
        if node in seen:continue
        seen.add(node);pending.extend(adjacency.get(node,()))
    return False


def main():
    root=Path(__file__).resolve().parents[1]
    cases=json.loads((root/'frozen/cases.json').read_text());graphs={c['id']:c for c in cases if 'uniformity_graph'in c};checks=[]
    for name,case in graphs.items():
        actual=reaches(case['uniformity_graph']['edges'])
        assert actual==case['graph_reaches_nonuniform'],name
        checks.append({'case':name,'reaches_nonuniform':actual})
    mutations=[]
    for name in ['uniform_skipped_and_varying','helper_skipped_and_varying_cf','flow_loop_break_backedge']:
        edges=graphs[name]['uniformity_graph']['edges'];changed=[edge for edge in edges if edge[1]!='N']
        assert reaches(edges) and not reaches(changed),name
        mutations.append({'case':name,'mutation':'remove retained varying RHS/call/backedge terminal dependency','flips_expected_rejection':True})
    name='flow_whole_assignment_kills';edges=graphs[name]['uniformity_graph']['edges']
    assert not reaches(edges) and reaches(edges+[['R','old_value'],['old_value','N']])
    mutations.append({'case':name,'mutation':'retain killed old definition','flips_expected_acceptance':True})
    report={'candidate_used':False,'dated_rules':'WGSL CRD2026-10-21 sections15.2.5/6/7/8','method':'Explicit manually derived decisive graph slices plus independent BFS, not author Boolean analysis','checks':checks,'mutations':mutations,'passed':True}
    (root/'ORACLE_GRAPH_CONTROLS.json').write_text(json.dumps(report,indent=2)+'\n')
    print(len(checks),'graphs and',len(mutations),'mutations passed')
if __name__=='__main__':main()
