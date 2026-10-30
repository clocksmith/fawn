# SPDX-License-Identifier: MIT
"""Canonical global-root pointer specialization, not hardware qualification."""
from pathlib import Path
import tempfile
from test_native_builtins import compile_shader

STORAGE = '''requires unrestricted_pointer_parameters;
@group(0)@binding(0)var<storage,read_write>out:array<u32>;
fn add(p:ptr<storage,u32,read_write>,x:u32){*p+=x;}
fn forward(p:ptr<storage,u32,read_write>){add(p,3u);}
@compute @workgroup_size(1)fn main(){let i=out[0];forward(&out[i]);}'''
PRIVATE = '''struct S { a:array<u32,3> } var<private>state:S;
@group(0)@binding(0)var<storage,read_write>out:array<u32>;
fn add(p:ptr<private,u32>,x:u32){*p+=x;}
fn forward(p:ptr<private,u32>){add(p,3u);}
@compute @workgroup_size(1)fn main(){let i=out[0];state.a[i]=2u;forward(&state.a[i]);out[1]=state.a[i];}'''
ATOMIC = '''var<workgroup>cells:array<atomic<u32>,4>;
@group(0)@binding(0)var<storage,read_write>out:array<u32>;
fn add(p:ptr<workgroup,atomic<u32>>)->u32{return atomicAdd(p,3u);}
@compute @workgroup_size(1)fn main(){let i=out[0];out[1]=add(&cells[i]);}'''
LENGTH = '''struct S { a:u32,data:array<u32> }
@group(0)@binding(0)var<storage,read_write>out:S;
fn count(p:ptr<storage,array<u32>,read_write>)->u32{return arrayLength(p);}
@compute @workgroup_size(1)fn main(){let p=&out.data;out.a=count(p);}'''
UNIFORM = '''struct S { value:vec4u }
@group(0)@binding(0)var<uniform>input:S;
@group(0)@binding(1)var<storage,read_write>out:array<u32>;
fn read_value(p:ptr<uniform,u32>)->u32{return *p;}
@compute @workgroup_size(1)fn main(){out[0]=read_value(&input.value.x);}'''
# A vector component is not addressable: use a scalar structure leaf instead.
UNIFORM = UNIFORM.replace('value:vec4u', '@size(16)value:u32').replace('input.value.x', 'input.value')
IMMEDIATE = '''requires immediate_address_space;
struct S { value:u32 } var<immediate>input:S;
@group(0)@binding(0)var<storage,read_write>out:array<u32>;
fn read_value(p:ptr<immediate,u32>)->u32{return *p;}
@compute @workgroup_size(1)fn main(){out[0]=read_value(&input.value);}'''
ALIAS = '''@group(0)@binding(0)var<storage,read_write>a:array<u32>;
fn read_value(p:ptr<storage,u32,read_write>,q:ptr<storage,u32,read_write>)->u32{return *p+*q;}
@compute @workgroup_size(1)fn main(){a[2]=read_value(&a[0],&a[1]);}'''
DIRECT = '''@group(0)@binding(0)var<storage,read_write>a:array<u32>;
fn direct()->u32{return a[0];}
fn read_value(p:ptr<storage,u32,read_write>)->u32{return *p+direct();}
@compute @workgroup_size(1)fn main(){a[2]=read_value(&a[1]);}'''


def main():
    with tempfile.TemporaryDirectory(prefix='doe-global-pointers-') as temporary:
        directory = Path(temporary)
        for name, source in (('storage', STORAGE), ('private', PRIVATE), ('atomic', ATOMIC),
                             ('length', LENGTH), ('uniform', UNIFORM), ('immediate', IMMEDIATE),
                             ('alias_read', ALIAS), ('direct_read', DIRECT),
                             ('scalar_same_root', STORAGE.replace('forward(&out[i]);', 'add(&out[i],out[0]);'))):
            for target in ('spirv', 'msl', 'hlsl'):
                output = compile_shader(directory, name, source, target)
                if target == 'hlsl' and name == 'storage':
                    text = output.read_text()
                    assert 'inout uint' not in text and 'r0[min(uint(' in text and '] = asuint(' in text
        invalid = [
            ALIAS.replace('return *p+*q;', '*p=7u;return *q;'),
            DIRECT.replace('return *p+direct();', '*p=7u;return direct();'),
            DIRECT.replace('return a[0];', 'a[0]=7u;return 0u;'),
            DIRECT.replace('fn direct()->u32{return a[0];}',
                'fn store_value(q:ptr<storage,u32,read_write>){*q=9u;}fn direct()->u32{store_value(&a[0]);return 0u;}'),
            UNIFORM.replace('return *p;', '*p=7u;return *p;'),
            IMMEDIATE.replace('return *p;', '*p=7u;return *p;'),
            STORAGE.replace('ptr<storage,u32,read_write>', 'ptr<storage,u32,write>'),
            PRIVATE.replace('ptr<private,u32>', 'ptr<private,u32,read_write>'),
        ]
        for index, source in enumerate(invalid):
            compile_shader(directory, f'invalid{index}', source, 'spirv', False)
    print('global pointer roots, captured paths, atomics, tails, readonly and alias controls passed')

if __name__ == '__main__':
    main()
