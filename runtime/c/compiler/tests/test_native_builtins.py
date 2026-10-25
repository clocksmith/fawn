# SPDX-License-Identifier: MIT
"""Derived acceptance controls; no SDK or physical-device qualification."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
CLI = os.environ.get("DOE_COMPILER_CLI", str(ROOT / "runtime/c/compiler/build/doe-compiler-c"))
VALIDATOR = os.environ.get("SPIRV_VAL", "spirv-val")

PACK = """@group(0) @binding(0) var<storage,read_write>a:array<vec4f>;
@compute @workgroup_size(1)fn main(){let x=a[0];
a[1]=unpack4x8snorm(pack4x8snorm(x));a[2]=unpack4x8unorm(pack4x8unorm(x));
a[3]=vec4f(unpack2x16snorm(pack2x16snorm(x.xy)),unpack2x16unorm(pack2x16unorm(x.zw)));
a[4]=vec4f(unpack2x16float(pack2x16float(x.xy)),quantizeToF16(x.zw));a[5]=quantizeToF16(x);}"""
DERIVATIVES = """fn f(x:vec4f)->vec4f{return dpdx(x)+dpdy(x)+fwidth(x)+dpdxFine(x)+dpdyFine(x)+fwidthFine(x)+dpdxCoarse(x)+dpdyCoarse(x)+fwidthCoarse(x);}
@fragment fn main(@location(0)x:vec4f)->@location(0)vec4f{return f(x);}"""
SUBGROUP = """enable subgroups;
@group(0)@binding(0)var<storage,read_write>a:array<vec4u>;
@compute @workgroup_size(32)fn main(@builtin(subgroup_invocation_id)i:u32,@builtin(subgroup_id)g:u32,@builtin(num_subgroups)n:u32){let x=a[0];
a[1]=subgroupBallot(i<16u);a[2]=subgroupBroadcast(x,0u);a[3]=subgroupBroadcastFirst(x);
a[4]=subgroupShuffle(x,i);a[5]=subgroupShuffleXor(x,1u);a[6]=subgroupShuffleUp(x,1u);a[7]=subgroupShuffleDown(x,1u);
a[8]=vec4u(u32(subgroupAll(i<32u)),u32(subgroupAny(i==0u)),u32(subgroupElect()),subgroupAdd(i));
a[9]=vec4u(g,n,quadBroadcast(i,0u),quadSwapX(i)+quadSwapY(i)+quadSwapDiagonal(i));}"""
FRAGMENT = """enable subgroups;
@fragment fn main(@builtin(subgroup_size)n:u32,@builtin(subgroup_invocation_id)i:u32)->@location(0)vec4u{
return vec4u(n,i,quadBroadcast(i,0u),quadSwapX(i)+quadSwapY(i)+quadSwapDiagonal(i));}"""


def compile_shader(directory, name, source, target, valid=True):
    shader = directory / (name + ".wgsl")
    shader.write_text(source)
    output = directory / (name + "." + target)
    result = subprocess.run([CLI, "--shader-path", str(shader), "--out", str(output), "--target", target], capture_output=True, text=True)
    if not valid:
        assert result.returncode != 0 and not output.exists(), (source, result.stderr)
        return None
    assert result.returncode == 0, result.stderr
    if target == "spirv":
        subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(output)], check=True)
    return output


def main():
    with tempfile.TemporaryDirectory(prefix="doe-native-builtins-") as temporary:
        directory = Path(temporary)
        for name, source in (("packing", PACK), ("derivatives", DERIVATIVES), ("subgroups", SUBGROUP), ("fragment", FRAGMENT)):
            for target in ("spirv", "msl", "hlsl"):
                compile_shader(directory, name, source, target)
        constants = """@group(0)@binding(0)var<storage,read_write>a:array<u32>;
@compute @workgroup_size(1)fn main(){a[0]=pack4x8snorm(vec4f(-1,-0.5,0.5,1));
a[1]=pack4x8unorm(vec4f(0,0.5,1,2));a[2]=pack2x16snorm(vec2f(-0.5,0.5));
a[3]=pack2x16float(vec2f(1.5,-2));a[4]=bitcast<u32>(quantizeToF16(1.00048828125));}"""
        path = compile_shader(directory, "constant", constants, "spirv")
        words = struct.unpack("<" + "I" * (path.stat().st_size // 4), path.read_bytes())
        payloads, offset = set(), 5
        while offset < len(words):
            count, opcode = words[offset] >> 16, words[offset] & 65535
            if opcode == 43 and count == 4:
                payloads.add(words[offset + 3])
            offset += count
        assert {0x7F40C181, 0xFFFF8000, 0x4000C001, 0xC0003E00, 0x3F800000} <= payloads
        invalid = [
            "@compute @workgroup_size(1)fn main(){let x=dpdx(1.0);}",
            "@fragment fn main(@location(0)x:f32)->@location(0)f32{if(x>0.0){return dpdx(x);}return 0.0;}",
            "enable subgroups;@compute @workgroup_size(1)fn main(){let x=subgroupBroadcast(1u,128u);}",
            "enable subgroups;@compute @workgroup_size(1)fn main(){let x=quadBroadcast(1u,4u);}",
            "enable subgroups;@compute @workgroup_size(1)fn main(@builtin(subgroup_invocation_id)i:u32){let x=subgroupShuffleXor(1u,i);}",
            "const x=pack2x16float(vec2f(65505.0,0.0));@compute @workgroup_size(1)fn main(){}",
            "const x=quantizeToF16(65505.0);@compute @workgroup_size(1)fn main(){}",
        ]
        for i, source in enumerate(invalid):
            compile_shader(directory, "invalid" + str(i), source, "spirv", False)
    print("native builtin derived acceptance: pass (SDK/device acceptance separate)")


if __name__ == "__main__":
    main()
