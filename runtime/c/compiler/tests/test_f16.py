# SPDX-License-Identifier: MIT
"""Source-to-target f16 developer regression; not physical GPU qualification."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
CLI = os.environ.get("DOE_COMPILER_CLI", str(ROOT / "runtime/c/compiler/build/doe-compiler-c"))
VALIDATOR = os.environ.get("SPIRV_VAL", "spirv-val")


def run(source, target, directory, name):
    path = directory / (name + ".wgsl")
    path.write_text(source)
    output = directory / (name + "." + target)
    subprocess.run([CLI, "--shader-path", str(path), "--out", str(output),
                    "--target", target], check=True, capture_output=True)
    if target == "spirv":
        subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(output)], check=True)
    return output


def main():
    consumers = [ROOT / "bench/inference-pipeline/kernels" / name for name in (
        "matmul_logits_forward_f16accum.wgsl", "matmul_logits_f16accum_wg64.wgsl",
        "matmul_logits_f16accum_wg128.wgsl", "matmul_logits_f16accum_wg256.wgsl")]
    layout = """enable f16;
struct Data { a: f16, b: vec3h, c: mat2x2h, }
@group(0) @binding(0) var<storage,read_write> data:array<Data>;
@compute @workgroup_size(1) fn main(){data[0].a=1.00048828125h;
data[0].b=vec3h(1.5h,-2.0h,0.000000059604644775390625h);
data[0].c=mat2x2h(vec2h(1.0h,2.0h),vec2h(3.0h,4.0h));} """
    uniform = """enable f16; struct Data { a:mat2x2h,b:f16, }
@group(0) @binding(0) var<uniform> data:Data;
@group(0) @binding(1) var<storage,read_write> out:array<f16>;
@compute @workgroup_size(1) fn main(){out[0]=data.a[1][0]+data.b;}"""
    with tempfile.TemporaryDirectory(prefix="doe-f16-") as temporary:
        directory = Path(temporary)
        subprocess.run([str(Path(CLI).with_name("test_f16")), str(directory)], check=True)
        for tail in sorted(directory.glob("tail*.spv")):
            subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(tail)], check=True)
        builtins = """enable f16;
@group(0) @binding(0) var<storage,read_write> a:array<vec2h>;
@compute @workgroup_size(1) fn main(){let x=a[0];
a[1]=smoothstep(vec2h(2h),vec2h(0h),x)+saturate(x);
a[2]=vec2h(dot(x,x),length(x)); a[3]=normalize(x); }"""
        bitcasts = """enable f16;
@group(0) @binding(0) var<storage,read_write> a:array<vec2u>;
@compute @workgroup_size(1) fn main(){let h=bitcast<vec4h>(a[0]);
a[1]=bitcast<vec2u>(h); let f=bitcast<vec2f>(h);
a[2]=bitcast<vec2u>(bitcast<vec4h>(f));
let s=bitcast<i32>(h.xy); a[3]=vec2u(bitcast<u32>(bitcast<vec2h>(s)),0u);}"""
        sources = [p.read_text() for p in consumers] + [layout, uniform, builtins, bitcasts]
        for i, source in enumerate(sources):
            for target in ("spirv", "msl", "hlsl"):
                run(source, target, directory, f"case{i}-{target}")
        # Independent Python IEEE binary16 packing checks emitted scalar constants.
        values = ("1.00048828125", "1.00146484375", "65504.0", "0.000000059604644775390625")
        for i, value in enumerate(values):
            source = f"enable f16; @group(0) @binding(0) var<storage,read_write> a:array<f16>; @compute @workgroup_size(1) fn main(){{a[0]={value}h;}}"
            output = run(source, "spirv", directory, f"literal{i}")
            words = struct.unpack("<" + "I" * (output.stat().st_size // 4), output.read_bytes())
            half_types, constants = set(), []
            cursor = 5
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xffff
                operands = words[cursor + 1:cursor + length]
                if opcode == 22 and operands[1] == 16:
                    half_types.add(operands[0])
                if opcode == 43 and operands[0] in half_types:
                    constants.append(operands[2])
                cursor += length
            expected = int.from_bytes(struct.pack("<e", float(value)), "little")
            assert expected in constants, (value, expected, constants)
        for i, (low, high) in enumerate((("1.0", "-2.0"), ("-0.0", "0.000000059604644775390625"))):
            expected = int.from_bytes(struct.pack("<ee", float(low), float(high)), "little")
            source = f"enable f16; @group(0) @binding(0) var<storage,read_write> a:array<u32>; @compute @workgroup_size(1) fn main(){{a[0]=bitcast<u32>(vec2h({low}h,{high}h));}}"
            output = run(source, "spirv", directory, f"bitcast-constant{i}")
            words = struct.unpack("<" + "I" * (output.stat().st_size // 4), output.read_bytes())
            cursor, values = 5, []
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xffff
                if opcode == 43 and length == 4:
                    values.append(words[cursor + 3])
                cursor += length
            assert expected in values, (expected, values)
        override_source = directory / "half-override.wgsl"
        override_source.write_text("enable f16; override h:f16; @group(0) @binding(0) var<storage,read_write> a:array<f16>; @compute @workgroup_size(1) fn main(){a[0]=h;}")
        for i, value in enumerate(("1.00048828125", "-0.0", "0.000000059604644775390625", "65504")):
            output = directory / f"half-override{i}.spv"
            subprocess.run([CLI, "--shader-path", str(override_source), "--out", str(output),
                            "--mode", "vulkan-compute-runtime", "--overrides", "h=" + value], check=True)
            subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(output)], check=True)
            words = struct.unpack("<" + "I" * (output.stat().st_size // 4), output.read_bytes())
            expected = int.from_bytes(struct.pack("<e", float(value)), "little")
            cursor, halves, found = 5, set(), False
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xffff
                if opcode == 22 and words[cursor + 2] == 16:
                    halves.add(words[cursor + 1])
                if opcode == 43 and words[cursor + 1] in halves and words[cursor + 3] == expected:
                    found = True
                cursor += length
            assert found, (value, expected)
        output = directory / "half-override-overflow.spv"
        rejected = subprocess.run([CLI, "--shader-path", str(override_source), "--out", str(output),
                                   "--mode", "vulkan-compute-runtime", "--overrides", "h=65520"], capture_output=True)
        assert rejected.returncode != 0 and not output.exists()
        for i, bad in enumerate(("@compute @workgroup_size(1) fn main(){let x=1h;}",
                "@compute @workgroup_size(1) fn main(){var x:f16;}",
                "enable f16; @compute @workgroup_size(1) fn main(){let x=65520h;}",
                "enable f16; @compute @workgroup_size(1) fn main(){let x=bitcast<vec2h>(0x7c00u);}",
                "enable f16; @compute @workgroup_size(1) fn main(){let x=bitcast<f16>(1u);}",
                "enable f16; @compute @workgroup_size(1) fn main(){let x=bitcast<u32>(vec3h(1h));}",
                "enable f16; @compute @workgroup_size(1) fn main(){let x=bitcast<mat2x2h>(mat2x2h());}")):
            path = directory / f"bad{i}.wgsl"
            path.write_text(bad)
            output = directory / f"bad{i}.spv"
            result = subprocess.run([CLI, "--shader-path", str(path), "--out", str(output)], capture_output=True)
            assert result.returncode != 0 and not output.exists()
    print("f16 unchanged consumers, three target emissions, SPIR-V validation, layout and literal oracle passed")


if __name__ == "__main__":
    main()
