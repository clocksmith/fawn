"""Independent source-schema packing and expectation model for M10.

This module never reads a compiler library, reflection, target IDs or opcodes.
All layout arithmetic is exact integer arithmetic; graph traversal is iterative.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from fractions import Fraction
import struct

MAX64 = (1 << 64) - 1
NONE = (1 << 32) - 1
LEAVES = {'bool': 1, 'i32': 2, 'u32': 3, 'f32': 4}


def checked(n: int) -> int:
    if not 0 <= n <= MAX64:
        raise ValueError('layout arithmetic outside uint64')
    return n


def aligned(n: int, a: int) -> int:
    return checked(((n + a - 1) // a) * a)


@dataclass
class Schema:
    types: dict = field(default_factory=lambda: {k: {'kind': 'scalar', 'leaf': k} for k in LEAVES})
    declared: list[str] = field(default_factory=list)

    def vec(self, n: int, leaf: str) -> str:
        key = f'vec{n}<{leaf}>'
        self.types[key] = {'kind': 'vector', 'element': leaf, 'count': n}
        return key

    def array(self, element: str, n: int | None) -> str:
        key = f'array<{element}' + (f',{n}>' if n is not None else '>')
        self.types[key] = {'kind': 'array' if n is not None else 'runtime_array', 'element': element, 'count': n}
        return key

    def structure(self, name: str, members: list[tuple]) -> str:
        if name in self.types or not members:
            raise ValueError('duplicate or empty oracle struct')
        self.types[name] = {'kind': 'struct', 'members': [
            {'name': m[0], 'type': m[1], 'align': m[2] if len(m) > 2 else 0,
             'size': m[3] if len(m) > 3 else 0} for m in members]}
        self.declared.append(name)
        return name

    def declarations(self) -> str:
        result = []
        for name in self.declared:
            rows = []
            for m in self.types[name]['members']:
                prefix = (f'@align({m["align"]}u) ' if m['align'] else '')
                prefix += f'@size({m["size"]}u) ' if m['size'] else ''
                rows.append(prefix + m['name'] + ':' + m['type'])
            result.append('struct ' + name + '{' + ','.join(rows) + ',}')
        return ''.join(result)

    def layouts(self) -> dict:
        done, active = {}, set()
        for root in self.types:
            work = [(root, False)]
            while work:
                name, exiting = work.pop()
                if name in done:
                    continue
                t = self.types[name]
                if not exiting:
                    if name in active:
                        raise ValueError('oracle type cycle')
                    active.add(name)
                    work.append((name, True))
                    deps = ([m['type'] for m in t['members']] if t['kind'] == 'struct'
                            else [t['element']] if 'element' in t else [])
                    work.extend((dep, False) for dep in reversed(deps) if dep not in done)
                    continue
                active.remove(name)
                kind = t['kind']
                if kind == 'scalar':
                    info = dict(size=4, align=4, stride=0, host=name != 'bool', construct=True, tail=None)
                elif kind == 'vector':
                    n = t['count']
                    info = dict(size=4*n, align=8 if n == 2 else 16, stride=0,
                                host=done[t['element']]['host'], construct=True, tail=None)
                elif kind in {'array', 'runtime_array'}:
                    e = done[t['element']]
                    if e['size'] is None or not e['construct']:
                        raise ValueError('oracle array element is not fixed constructible')
                    stride = aligned(e['size'], e['align'])
                    runtime = kind == 'runtime_array'
                    info = dict(size=None if runtime else checked(stride*t['count']), align=e['align'],
                                stride=stride, host=e['host'], construct=not runtime,
                                tail=(name, 0, stride) if runtime else None)
                else:
                    offset, align, host, construct, members, tail = 0, 1, True, True, [], None
                    for index, m in enumerate(t['members']):
                        e = done[m['type']]
                        a, z = m['align'] or e['align'], m['size'] or e['size']
                        if a < e['align'] or a & (a-1) or (z is not None and z < e['size']):
                            raise ValueError('invalid explicit layout in oracle schema')
                        offset = aligned(offset, a)
                        members.append(dict(offset=offset, align=a, size=z))
                        align, host, construct = max(align,a), host and e['host'], construct and e['construct']
                        if e['tail']:
                            if index != len(t['members'])-1 or self.types[m['type']]['kind'] != 'runtime_array' or m['size']:
                                raise ValueError('invalid oracle runtime tail')
                            tail = (m['type'], offset, e['stride'])
                        else:
                            offset = checked(offset + z)
                    info = dict(size=None if tail else aligned(offset,align), align=align, stride=0,
                                host=host, construct=construct, members=members, tail=tail)
                done[name] = info
        return done

    def minimum(self, root: str) -> int:
        x = self.layouts()[root]
        return x['size'] if x['tail'] is None else aligned(x['tail'][1]+x['tail'][2],x['align'])

    def leaf_offsets(self, root: str, value: object, base: int = 0) -> list[tuple[int,str,int]]:
        layout, out = self.layouts(), []
        work = [(root, value, base)]
        while work:
            name, val, offset = work.pop()
            t, l = self.types[name], layout[name]
            if t['kind'] == 'scalar':
                out.append((offset, name, int(val)))
            elif t['kind'] == 'struct':
                work.extend((m['type'],val[i],offset+l['members'][i]['offset'])
                            for i,m in reversed(list(enumerate(t['members']))))
            else:
                count = len(val) if t['kind'] == 'runtime_array' else t['count']
                if len(val) != count:
                    raise ValueError('oracle value element count mismatch')
                stride = 4 if t['kind'] == 'vector' else l['stride']
                work.extend((t['element'], val[i], offset+i*stride) for i in reversed(range(count)))
        return out

    def pack(self, root: str, value: object, size: int | None = None, fill: int = 0xD7) -> bytes:
        leaves = self.leaf_offsets(root,value)
        minimum = self.minimum(root)
        size = max([minimum] + [off+4 for off,_,_ in leaves]) if size is None else size
        data = bytearray([fill]*size)
        for off,leaf,bits in leaves:
            if leaf == 'bool' or off+4 > size:
                raise ValueError('invalid host-shareable packed value')
            struct.pack_into('<I', data, off, bits & 0xffffffff)
        return bytes(data)

    def reflection(self, resources: list[dict], entry: str = 'main', local: tuple = (1,1,1)) -> dict:
        layouts = self.layouts()
        order = list(self.declared)
        seen = set(order)
        def discover(root: str) -> None:
            work = [root]
            while work:
                name = work.pop()
                if name not in seen:
                    seen.add(name); order.append(name)
                t = self.types[name]
                children = ([m['type'] for m in t['members']] if t['kind'] == 'struct'
                            else [t['element']] if 'element' in t else [])
                work.extend(reversed([c for c in children if c not in seen]))
        for name in self.declared:
            discover(name)
        for resource in resources:
            discover(resource['type'])
        indices = {name:i for i,name in enumerate(order)}
        types, members = [], []
        for name in order:
            t,l = self.types[name],layouts[name]
            kind=t['kind']; first=len(members)
            flags=int(l['construct'])|2*int(l['host'])|4*int(l['size'] is not None)|8*int(l['tail'] is not None)
            for m,ml in zip(t.get('members',[]),l.get('members',[])):
                members.append(dict(name=m['name'],type_index=indices[m['type']],flags=int(bool(m['align']))|2*int(bool(m['size'])),
                                    offset_bytes=ml['offset'],size_bytes=ml['size'] or 0,alignment_bytes=ml['align'],
                                    explicit_align_bytes=m['align'],explicit_size_bytes=m['size']))
            types.append(dict(kind={'scalar':1,'vector':2,'array':3,'struct':4,'runtime_array':5}[kind],flags=flags,
                              name=name if kind=='struct' else '',scalar_kind=LEAVES.get(name,0),
                              element_type=indices[t['element']] if 'element' in t else NONE,
                              vector_width=t['count'] if kind=='vector' else 0,array_count=t['count'] if kind=='array' else 0,
                              first_member=first if kind=='struct' else 0,member_count=len(t.get('members',[])),
                              size_bytes=l['size'] or 0,alignment_bytes=l['align'],stride_bytes=l['stride']))
        result_resources=[]
        for r in resources:
            l=layouts[r['type']]; tail=l['tail']; uniform=r['kind']==2
            result_resources.append(dict(name=r['name'],group=r['group'],binding=r['binding'],kind=r['kind'],access=r['access'],
                type_index=indices[r['type']],flags=int(tail is not None),minimum_binding_size_bytes=self.minimum(r['type']),
                type_alignment_bytes=l['align'],address_space_alignment_bytes=aligned(l['align'],16) if uniform and self.types[r['type']]['kind'] in {'struct','array'} else l['align'],
                runtime_array_offset_bytes=tail[1] if tail else 0,runtime_array_stride_bytes=tail[2] if tail else 0,
                runtime_array_type=indices[tail[0]] if tail else NONE,reserved=0))
        return dict(entry=entry,workgroup_size=list(local),declared_struct_count=len(self.declared),types=types,members=members,resources=result_resources)


def resource(name: str, type_name: str, binding: int, access: int = 1, kind: int = 1, group: int = 0) -> dict:
    return dict(name=name,type=type_name,binding=binding,access=access,kind=kind,group=group)


def declarations(resources: list[dict]) -> str:
    return ''.join(f'@group({r["group"]}) @binding({r["binding"]}) var<' +
                   ('uniform' if r['kind']==2 else 'storage,'+('read' if r['access']==1 else 'read_write'))+
                   f'> {r["name"]}:{r["type"]};' for r in resources)


def raw_words(words: list[int]) -> bytes:
    return b''.join(struct.pack('<I',w & 0xffffffff) for w in words)


def expected_writes(words: list[int], offset: int = 0) -> dict[int,int]:
    return {offset+i:b for i,b in enumerate(raw_words(words))}
