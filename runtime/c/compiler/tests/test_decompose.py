# SPDX-License-Identifier: MIT
"""Typed floating decomposition controls, distinct from SDK/device qualification."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
CLI = os.environ.get("DOE_COMPILER_CLI", str(ROOT / "runtime/c/compiler/build/doe-compiler-c"))
VALIDATOR = os.environ.get("SPIRV_VAL", "spirv-val")


def compile_source(directory, name, source, target, extra=()):
    path = directory / (name + ".wgsl")
    path.write_text(source)
    output = directory / (name + "." + target)
    result = subprocess.run([CLI, "--shader-path", str(path), "--out", str(output),
                             "--target", target, *extra], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    if target == "spirv":
        subprocess.run([VALIDATOR, "--target-env", "vulkan1.1", str(output)], check=True)
    return output


def main():
    with tempfile.TemporaryDirectory(prefix="doe-decompose-") as temporary:
        directory = Path(temporary)
        for leaf in ("f32", "f16"):
            for width in range(1, 5):
                value_type = leaf if width == 1 else f"vec{width}<{leaf}>"
                exponent = "i32" if width == 1 else f"vec{width}<i32>"
                source = ("enable f16;" if leaf == "f16" else "") + f"""
@group(0) @binding(0) var<storage,read_write> a:array<{value_type}>;
@compute @workgroup_size(1) fn main(){{let x=a[0];let f=frexp(x);var m=modf(x);
a[1]=f.fract;a[2]={value_type}(f.exp);a[3]=m.fract;a[4]=m.whole;
a[5]=ldexp(x,{exponent}(1));}}"""
                for target in ("spirv", "msl", "hlsl"):
                    compile_source(directory, f"runtime-{leaf}-{width}-{target}", source, target)
        constant = """const f=frexp(11.5);const m=modf(-11.5);
@group(0) @binding(0) var<storage,read_write> a:array<vec4f>;
@compute @workgroup_size(1) fn main(){let v=frexp(vec2(1.5,-4.0));
a[0]=vec4f(f.fract,f32(f.exp),m.fract,m.whole);a[1]=vec4f(ldexp(0.75,3));
a[2]=vec4f(v.fract,vec2f(v.exp));
let r=array(frexp(1.5f),frexp(2.0f));a[3]=vec4f(r[0].fract);}"""
        for target in ("spirv", "msl", "hlsl"):
            output = compile_source(directory, "constant-" + target, constant, target)
            if target == "spirv":
                words = struct.unpack("<" + "I" * (output.stat().st_size // 4), output.read_bytes())
                cursor, values = 5, set()
                while cursor < len(words):
                    length, opcode = words[cursor] >> 16, words[cursor] & 0xffff
                    if opcode == 43 and length == 4:
                        values.add(words[cursor + 3])
                    cursor += length
                for expected in (0.71875, 4.0, -0.5, -11.0, 6.0):
                    bits = struct.unpack("<I", struct.pack("<f", expected))[0]
                    assert bits in values, (expected, bits, values)
        override = """override power:i32=129;
@group(0) @binding(0) var<storage,read_write> a:array<f32>;
@compute @workgroup_size(1) fn main(){a[0]=ldexp(a[0],power);}"""
        compile_source(directory, "override", override, "spirv",
                       ("--mode", "vulkan-compute-runtime", "--overrides", "power=128"))
        bad_expressions = ("frexp(1u)", "modf(true)", "ldexp(1f,1u)",
                           "ldexp(vec2f(1),1)", "ldexp(0f,129)", "frexp(1f).whole")
        for i, expression in enumerate(bad_expressions):
            path = directory / f"invalid{i}.wgsl"
            path.write_text(f"@compute @workgroup_size(1) fn main(){{let x={expression};}}")
            output = directory / f"invalid{i}.spv"
            result = subprocess.run([CLI, "--shader-path", str(path), "--out", str(output)], capture_output=True)
            assert result.returncode != 0 and not output.exists()
        for i, (source, extra) in enumerate((
            ("enable f16; @compute @workgroup_size(1) fn main(){let x=ldexp(0h,17);}", ()),
            ("@group(0) @binding(0) var<storage,read_write> a:array<f32>; @compute @workgroup_size(1) fn main(){a[0]=ldexp(a[0],129);}", ()),
            (override, ("--mode", "vulkan-compute-runtime", "--overrides", "power=129")),
        )):
            path = directory / f"bound{i}.wgsl"
            path.write_text(source)
            output = directory / f"bound{i}.spv"
            result = subprocess.run([CLI, "--shader-path", str(path), "--out", str(output), *extra], capture_output=True)
            assert result.returncode != 0 and not output.exists()
    print("frexp/modf/ldexp: abstract constants, f32/f16 widths, all targets, validators and exponent bounds passed")


if __name__ == "__main__":
    main()
