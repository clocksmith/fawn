"""Postinspection GCC callgraph/stack accounting, not a fixture or oracle change.

Counts each non-recursive library frame once as a deliberately loose base, then
uses source-reviewed depth multipliers for each recursive strongly connected
component. Excludes libc/libm/OS/sanitizer frames and does not claim runtime peak.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native-evidence',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();defined={};edges=[];inputs={}
    for path in sorted((a.native_evidence/'build').glob('*.ci.gz')):
        inputs[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
        text=gzip.decompress(path.read_bytes()).decode()
        for title,label in re.findall(r'node: \{ title: "([^"]+)" label: "([^"]+)"',text):
            size=re.search(r'\\n(\d+) bytes',label)
            if size:defined[title]=dict(bytes=int(size.group(1)),name=label.split('\\n')[0],translation_unit=path.name)
        edges+=re.findall(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"',text)
    graph={node:set() for node in defined}
    for caller,callee in edges:
        if caller in graph and callee in graph:graph[caller].add(callee)
    index={};low={};stack=[];active=set();components=[]
    def visit(node):
        index[node]=low[node]=len(index);stack.append(node);active.add(node)
        for callee in graph[node]:
            if callee not in index:visit(callee);low[node]=min(low[node],low[callee])
            elif callee in active:low[node]=min(low[node],index[callee])
        if low[node]==index[node]:
            group=[]
            while True:
                item=stack.pop();active.remove(item);group.append(item)
                if item==node:break
            components.append(group)
    for node in graph:
        if node not in index:visit(node)
    recursive=[sorted(group) for group in components if len(group)>1 or group[0] in graph[group[0]]]
    public={node for node in defined if node.startswith('doe_compiler_')}
    rec_nodes={node for group in recursive for node in group}
    base=sum(record['bytes'] for node,record in defined.items() if node not in public|rec_nodes)
    weights={frozenset(defined[node]['name'] for node in group):sum(defined[node]['bytes'] for node in group) for group in recursive}
    expected={frozenset(('expression.part.0','primary')):368,frozenset(('statement','block')):144,
              frozenset(('mandatory_rhs','lower_index','call_value','expression_value')):1216,
              frozenset(('lower_block',)):624,frozenset(('emit_block',)):256}
    assert weights==expected,(weights,expected)
    assert base==6152,base
    rows=[]
    for api,depth,control in [('doe_compiler_compile',18,4),('doe_compiler_compile_v2',18,4),('doe_compiler_compile_job',64,32)]:
        phases=dict(parse=368*(depth+2)+144*(3*control+4),
                    semantic=1216*(depth+2)+624*(3*control+4),
                    emission=256*(2*control+depth+4))
        wrapper=defined[api]['bytes']
        rows.append(dict(api=api,expression_depth=depth,control_depth=control,wrapper_bytes=wrapper,
                         conservative_acyclic_base=base,phase_recursive_bytes=phases,
                         combined_library_frame_bound=wrapper+base+max(phases.values())))
    frames=json.loads((a.native_evidence/'frames.json').read_text())
    assert len(frames)==len(defined)
    assert not any(row['size']>row['limit'] or 'unbounded' in row['qualifier'] for row in frames)
    output=dict(scope='GCC14.2 -O2 Linux x86-64 compiled-library frame upper accounting, excluding external library/OS/sanitizer frames, not measured runtime stack peak',
                status='PASS',definitions=len(defined),inputs=inputs,
                recursion_components=[dict(nodes=[defined[node] for node in group],weight=sum(defined[node]['bytes'] for node in group)) for group in recursive],
                excluded_external_callees=sorted({callee for _,callee in edges if callee not in graph}),
                frame_limit_failures=[],bounds=rows,
                explanation=['All non-recursive library frames are summed once, even across disjoint phases, overestimating every acyclic internal path.',
                    'Parsing, semantic lowering and target emission return before the next phase. Their recursive contributions therefore combine by max, not sum.',
                    'Expression SCC multiplier D+2 includes the accepted D depth, one historical uncharged outer comparison and a boundary frame. Call lowering walks argument syntax children, never callee bodies.',
                    'The deliberately loose control multiplier 3C+4 covers at most 2C+1 nested brace blocks, C direct else-if links, outer/rejected/init-update frames. Semantic control lowering traverses the same parsed tree.',
                    'Emission multiplier 2C+D+4 overestimates nested control/short-circuit IR and boundary frames. Calls and signatures are streamed, not recursively expanded.',
                    'Function count, formal count, argument count and call-graph depth add no C stack depth. Iterative graph and constant-dependency walks retain state in caller-owned records.'])
    a.out.write_text(json.dumps(output,indent=2,sort_keys=True)+'\n')
    print(json.dumps(rows,indent=2));return 0

if __name__=='__main__':raise SystemExit(main())
