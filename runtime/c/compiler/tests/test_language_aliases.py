# SPDX-License-Identifier: MIT
"""Shared SSA handle aliases and pointer-reference normalization controls."""
from pathlib import Path
import tempfile
from test_native_builtins import compile_shader

HANDLE = '''requires texture_and_sampler_let;
@group(0)@binding(0)var t:texture_2d<f32>;
@group(0)@binding(1)var s:sampler;
@group(0)@binding(2)var<storage,read_write>out:array<vec4f>;
fn sample(a:texture_2d<f32>,b:sampler)->vec4f{
 let x=a;let y=b;return textureSampleLevel(x,y,vec2f(0.5),0.0);}
@compute @workgroup_size(1)fn main(){let a=t;let b=s;out[0]=sample(a,b);}'''
POINTER = '''requires pointer_composite_access;
struct S { a:u32,b:array<u32,2> }
@group(0)@binding(0)var<storage,read_write>out:array<u32>;
fn put(p:ptr<function,S>){p.a=p.b[1];}
@compute @workgroup_size(1)fn main(){var x:S;let p=&x;p.b[1]=7u;put(p);out[0]=p.a;}'''

SUBOBJECT = '@group(0)@binding(0)var<storage,read_write>out:array<u32>;\nfn leaf(p:ptr<function,u32>,v:u32)->u32 { *p=*p+v; return *p; }\nfn middle(p:ptr<function,array<u32,3>>,i:u32)->u32 { return leaf(&(*p)[i],7u); }\nstruct S { n:u32, data:array<array<u32,3>,2> }\n@compute @workgroup_size(1)fn main(){var s:S;s.data[1][2]=5u;let j=out[0];out[1]=middle(&s.data[1],j);out[2]=s.data[1][2];out[3]=middle(&s.data[j+9u],0u);}\n'

VALUE_PATH = '@group(0)@binding(0)var<storage,read_write>out:array<u32>;\nfn leaf(p:ptr<function,u32>,j:u32)->u32{let a=array<u32,3>(3u,5u,7u);var sum=0u;for(var i=0u;i<2u;i++){sum=sum+a[j];}if(j<3u){*p=sum;}return *p;}\n@compute @workgroup_size(1)fn main(){var a:array<u32,2>;let j=out[0];out[1]=leaf(&a[1],j);}\n'

def main():
    with tempfile.TemporaryDirectory(prefix="doe-language-aliases-") as temporary:
        directory = Path(temporary)
        for name, source in (("handle", HANDLE), ("pointer", POINTER), ("subobject", SUBOBJECT), ("value_path", VALUE_PATH)):
            for target in ("spirv", "msl", "hlsl"):
                compile_shader(directory, name, source, target)
        for index, source in enumerate((
            HANDLE.replace("let a=t;", "var a=t;"),
            HANDLE.replace("let a=t;", "const a=t;"),
            HANDLE.replace("let a=t;", "let a=t;a=t;"),
            POINTER.replace("p.a=p.b[1];", "p.a=p.no_such_field;"),
            POINTER.replace("fn put(p:ptr<function,S>){p.a=p.b[1];}",
                "fn put(p:ptr<function,S>,q:ptr<function,S>){p.a=q.a;}")
                .replace("put(p);", "put(p,p);"),
        )):
            compile_shader(directory, f"invalid{index}", source, "spirv", False)
    print("shared handle aliases and pointer composite controls passed")

if __name__ == "__main__":
    main()
