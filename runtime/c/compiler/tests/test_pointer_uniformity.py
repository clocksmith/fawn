# SPDX-License-Identifier: MIT
"""Pointer address/content and call write-through uniformity controls."""
from pathlib import Path
import tempfile
from test_native_builtins import compile_shader

PREFIX = 'fn put(p:ptr<function,u32>,x:u32){*p=x;}\n'
UNIFORM_WRITE = PREFIX + '''@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){
var x=i;let p=&x;put(p,1u);if(x==1u){workgroupBarrier();}}'''
VARYING_WRITE = UNIFORM_WRITE.replace('var x=i;', 'var x=0u;').replace('put(p,1u)', 'put(p,i)')
READ = '''fn read_value(p:ptr<function,u32>)->u32{return *p;}
@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){var x=i;
if(read_value(&x)==0u){workgroupBarrier();}}'''
SEPARATE = '''fn put(p:ptr<function,u32>,q:ptr<function,u32>,x:u32){*p=x;*q=1u;}
@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){var x=0u;var y=i;
put(&x,&y,i);if(y==1u){workgroupBarrier();}}'''
BRANCH = PREFIX + '''@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){
var x=0u;let p=&x;if(i==0u){put(p,1u);}if(x==1u){workgroupBarrier();}}'''
LOAD = '''var<workgroup>a:array<u32,4>;
fn read_value(p:ptr<workgroup,u32>)->u32{return workgroupUniformLoad(p);}
@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){let p=&a[0];
if(read_value(p)==0u){workgroupBarrier();}}'''
SUBOBJECT = '''struct S { a:u32,b:u32 }
fn put(p:ptr<function,u32>){*p=1u;}
@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){var x:S;x.b=i;
put(&x.a);if(x.b==0u){workgroupBarrier();}}'''
RETURN_BRANCH = '''fn put(p:ptr<function,u32>,x:u32){if(x==0u){*p=1u;return;}*p=2u;}
@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){var x=0u;
put(&x,i);if(x==0u){workgroupBarrier();}}'''
ALIAS = PREFIX + '''@compute @workgroup_size(4)fn main(@builtin(local_invocation_index)i:u32){
var x=0u;let p=&x;let q=p;*q=i;if(*p==0u){workgroupBarrier();}}'''


def main():
    with tempfile.TemporaryDirectory(prefix='doe-pointer-uniformity-') as temporary:
        directory = Path(temporary)
        for name, source in (('overwrite', UNIFORM_WRITE), ('separate', SEPARATE), ('uniform_load', LOAD),
                             ('uniform_read', READ.replace('var x=i;', 'var x=1u;')),
                             ('nested_write', UNIFORM_WRITE.replace('put(p,1u)', 'relay(p,1u)') +
                              '\nfn relay(p:ptr<function,u32>,x:u32){put(p,x);}')):
            for target in ('spirv', 'msl', 'hlsl'):
                compile_shader(directory, name, source, target)
        for index, source in enumerate((VARYING_WRITE, READ, BRANCH, SUBOBJECT, RETURN_BRANCH, ALIAS,
                                        VARYING_WRITE.replace('put(p,i)', 'relay(p,i)') + '\nfn relay(p:ptr<function,u32>,x:u32){put(p,x);}',
                                        SEPARATE.replace('if(y==1u)', 'if(x==1u)'),
                                        LOAD.replace('let p=&a[0]', 'let p=&a[i]'))):
            compile_shader(directory, f'invalid{index}', source, 'spirv', False)
    print('pointer pointee sensitivity, output dependencies, aliases and collective address controls passed')

if __name__ == '__main__':
    main()
