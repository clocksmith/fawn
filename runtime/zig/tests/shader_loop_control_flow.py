"""Execute emitted scalar control flow on the CPU; never GPU qualification.

Only the entry-point signature and target preamble are adapted to portable C++.
The emitted body is copied byte-for-byte and checked against fixed WGSL oracles.
Requires existing emitters and Zig; writes only a fresh explicit output directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


REPRODUCTION = """@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;
@compute @workgroup_size(8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  var total = 0u;
  for (var i = 0u; i < 4u; i++) {
    if (i == 1u) { i += 1u; continue; }
    total += i;
  }
  output[gid.x] = total;
}
"""


CASES = [
    ("for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { if (i == 1u) { i++; continue; } total += i; } output[gid.x] = total;", 3),
    ("nested-while-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { var j = 0u; while (j < 2u) { j++; if (j == 1u) { continue; } total += 10u; } if (i == 1u) { i++; continue; } total += i; } output[gid.x] = total;", 33),
    ("switch-for-continue", "var total = 0u; for (var i = 0u; i < 4u; i++) { switch i { case 1u: { i++; continue; } default: {} } total += i; } output[gid.x] = total;", 3),
    ("loop-continuing-break", "var total = 0u; var i = 0u; loop { if (i == 1u) { i++; continue; } total += i; continuing { i++; break if i >= 4u; } } output[gid.x] = total;", 3),
    ("switch-continuing-break", "var total = 0u; var i = 0u; loop { switch i { case 1u: { i += 2u; continue; } default: {} } total += i; continuing { i++; break if i >= 4u; } } output[gid.x] = total;", 0),
    ("shadowed-loop-index", "var trips = 0u; for (var i = 0u; i < 4u; i++) { trips++; if (trips >= 6u) { break; } { var i = 100u; continue; } } output[gid.x] = trips;", 4),
    ("shadowed-step", "var i = 0u; var trips = 0u; let step = 1u; loop { trips++; if (trips >= 6u) { break; } { let step = 10u; continue; } continuing { i += step; break if i >= 4u; } } output[gid.x] = trips;", 4),
    ("continuing-switch", "var i = 0u; var total = 0u; loop { i++; if (i == 2u) { continue; } total += i; continuing { switch i { case 1u: { total += 10u; } default: {} } break if i >= 3u; } } output[gid.x] = total;", 14),
    ("continuing-nested-loop", "var i = 0u; var total = 0u; loop { i++; if (i < 3u) { continue; } total += i; continuing { var j = 0u; loop { j++; if (j >= 2u) { break; } } total += j; break if i >= 3u; } } output[gid.x] = total;", 9),
    ("continuing-local", "var i = 0u; var total = 0u; loop { if (i == 1u) { i++; continue; } total += i; continuing { let step = 1u; i += step; break if i >= 4u; } } output[gid.x] = total;", 3),
    ("break-skips-continuing", "var i = 0u; var total = 0u; loop { total++; if (total == 2u) { break; } continuing { i += 10u; } } output[gid.x] = i;", 10),
    ("nested-for-continue", "var total = 0u; for (var i = 0u; i < 3u; i++) { for (var j = 0u; j < 4u; j++) { if (j == 1u) { j++; continue; } total += j; } } output[gid.x] = total;", 9),
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body_of(source: str, name: str) -> str:
    start = source.index("{", source.index("void " + name + "("))
    depth = 0
    for index in range(start, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise ValueError("Unterminated emitted entry point")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msl-emitter", type=Path, required=True)
    parser.add_argument("--hlsl-emitter", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repro-shader", type=Path, help="Optional byte-identical retained original reproduction")
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    receipt = {"scope": "CPU execution of unchanged emitted scalar bodies; no target shader compiler or GPU execution", "cases": [], "targets": []}
    for name, body, expected in CASES:
        shader = output / (name + ".wgsl")
        source = (REPRODUCTION if name == "for-continue" else
                  "@group(0) @binding(0) var<storage, read_write> output: array<u32, 8>;\n"
                  "@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) gid: vec3u) { " + body + " }\n")
        shader.write_text(source, encoding="utf-8", newline="\n")
        if name == "for-continue" and args.repro_shader:
            if args.repro_shader.read_text(encoding="utf-8") != REPRODUCTION:
                raise ValueError("Retained reproduction must match the frozen WGSL source")
            shader.write_bytes(args.repro_shader.read_bytes())
        receipt["cases"].append({"name": name, "wgslSha256": digest(shader), "expected": [expected] * 8})
    for target, emitter, entry in [("msl", args.msl_emitter, "main_kernel"), ("hlsl", args.hlsl_emitter, "main")]:
        fragments = ['extern "C" int printf(const char*, ...);\nusing uint = unsigned int;\nstruct uint3 { uint x, y, z; };\nstatic uint output[8];\ntemplate<class T> T min(T a, T b) { return a < b ? a : b; }\nuint min(uint a, int b) { return min(a, uint(b)); }\n']
        record = {"target": target, "emitterSha256": digest(emitter), "bodies": [], "commands": []}
        for index, (name, _, _) in enumerate(CASES):
            shader = output / (name + ".wgsl")
            emitted_path = output / (name + "." + target)
            command = [str(emitter), "--shader-path", str(shader), "--out", str(emitted_path)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            record["commands"].append({"command": command, "exitCode": result.returncode})
            result.check_returncode()
            emitted = emitted_path.read_text(encoding="utf-8")
            body = body_of(emitted, entry)
            fragments.append("void case_" + str(index) + "(uint3 gid) " + body + "\n")
            record["bodies"].append({"name": name, "emittedSha256": digest(emitted_path), "bodySha256": hashlib.sha256(body.encode()).hexdigest()})
        fragments.append('int main() { bool pass = true;\n')
        for index, (name, _, expected) in enumerate(CASES):
            fragments.append(f'for (uint n=0;n<8;n++) output[n]=0xdeadbeefu;\nfor (uint n=0;n<8;n++) case_{index}({{n,0,0}});\nprintf("{name}");\nfor (uint n=0;n<8;n++) {{ printf(" %u", output[n]); if (output[n]!={expected}u) pass=false; }} printf("\\n");\n')
        fragments.append('return pass ? 0 : 1; }\n')
        cpp = output / (target + "-cpu.cpp")
        cpp.write_text("".join(fragments), encoding="utf-8")
        exe = output / (target + "-cpu.exe")
        command = [str(args.zig), "cc", "-x", "c++", "-std=c++17", "-O0", str(cpp), "-o", str(exe)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (output / (target + "-compile.log")).write_text(built.stdout + built.stderr, encoding="utf-8")
        record.update(compileCommand=command, compileExitCode=built.returncode, cpuSourceSha256=digest(cpp))
        if built.returncode == 0:
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            (output / (target + "-run.log")).write_text(ran.stdout + ran.stderr, encoding="utf-8")
            record.update(runExitCode=ran.returncode, binarySha256=digest(exe), output=ran.stdout, passed=ran.returncode == 0)
        else:
            record["passed"] = False
        receipt["targets"].append(record)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print(target, "PASS" if record["passed"] else "FAIL", flush=True)
    if not all(row["passed"] for row in receipt["targets"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
