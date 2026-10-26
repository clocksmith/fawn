#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare real C/Zig native-source admission and DXBC output, not GPU semantics."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
ZIG_SOURCE = ROOT / "runtime/zig/src"
PRELUDE = "@compute @workgroup_size(1) fn main() {"
STORAGE = "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
CASES = [
    ("empty", PRELUDE + "}"),
    ("for", PRELUDE + "for(var i=0u;i<4u;i+=1u){let n=i+1u;} }"),
    ("forward-struct", "var<private> state:Later=Later(9u);" + PRELUDE +
     "let n=state.value;} struct Later{value:u32,}"),
    ("forward-type", PRELUDE + "let n=Later(9u);_ = n.value;} struct Later{value:u32,}"),
    ("composite-global", "const value=vec2i(-2,3)+vec2i(1,2);" + PRELUDE + "let n=value.x;}"),
    ("composite-literal", "const value=vec2i(1,2);" + PRELUDE + "let n=value.x;}"),
    ("override-id", "@id(7) override amount:u32=3u;" + PRELUDE + "let n=amount;}"),
    ("matrix-index", PRELUDE + "var m=mat2x2f(1,2,3,4);let n=m[1u][0u];}"),
    ("nested-members", "struct S{m:mat2x2f,v:vec2u,}" + PRELUDE +
     "var s:S;s.m[0]=vec2f(1);s.v.x=2u;let n=s.v.yx.x;}"),
    ("runtime-read", STORAGE + PRELUDE + "var i=1u;let n=data[i];}"),
    ("runtime-read-gid", STORAGE + "@compute @workgroup_size(1) fn main("
     "@builtin(global_invocation_id) gid:vec3u){let n=data[gid.x];}"),
    ("runtime-write", STORAGE + PRELUDE + "var i=1u;data[i]=2u;}"),
    ("fixed-read", "@group(0) @binding(0) var<storage,read> data:array<u32,4>;" +
     PRELUDE + "var i=1u;let n=data[i];}"),
    ("fixed-write-loop", "@group(0) @binding(0) var<storage,read_write> data:array<u32,4>;" +
     PRELUDE + "for(var i=0u;i<4u;i+=1u){data[i]=i;} }"),
    ("fixed-read-loop", PRELUDE + "var values:array<u32,4>;"
     "for(var i=0u;i<4u;i++){let n=values[i];}}"),
    ("constant-fold", PRELUDE + "let n=2u+3u;}"),
    ("switch-continuing", PRELUDE + "var n=0u;switch n{case 0u,1u:{n=2u;}default:{n=3u;}}"
     "loop{let old=n;continuing{n=old+1u;break if n>4u;}}}"),
    ("vertex", "@vertex fn main(@builtin(vertex_index) n:u32)->@builtin(position) vec4f{"
     "return vec4f(f32(n),0,0,1);}"),
    ("fragment", "@fragment fn main(@location(0) uv:vec2f)->@location(0) vec4f{"
     "return vec4f(uv,0,1);}"),
    ("unknown-name", PRELUDE + "let n=absent;}"),
    ("unknown-type", "fn helper(n:Absent){}"),
    ("duplicate-name", "fn same(){} fn same(){}"),
    ("type-mismatch", PRELUDE + "let n:u32=true;}"),
    ("duplicate-id", "@id(4) override a=1u;@id(4) override b=2u;" + PRELUDE + "}"),
    ("syntax", "fn main( {"),
    ("no-entry", "fn helper(){}"),
    ("derived-override", "override a:u32=3u;override b:u32=a+1u;" + PRELUDE + "let x=b;}"),
]

# These are observed retained-reference limitations, not parity passes. Do not
# weaken the canonical C compiler to reproduce them or silently change Zig.
# --strict keeps an exact-parity gate that fails on these differences too.
REFERENCE_DIFFERENCES = {
    "forward-struct": (0, "", 1, "TypeMismatch sema",
                       "C supports the valid private forward-struct initializer"),
    "composite-global": (0, "", 1, "UnsupportedConstruct ir_builder",
                         "C evaluates the valid composite constant expression"),
    "duplicate-id": (1, "InvalidAttribute sema", 0, "",
                     "C rejects duplicate override IDs which retained Zig admits"),
}


def run(args: list[str], timeout: int = 30) -> subprocess.CompletedProcess[bytes]:
    child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    return subprocess.CompletedProcess(args, child.returncode, stdout, stderr)


def container(data: bytes) -> list[tuple[str, int]]:
    """Check independently bounded container framing, not LLVM/DXIL semantics."""
    assert len(data) >= 32 and data[:4] == b"DXBC"
    version, size, count = struct.unpack_from("<III", data, 20)
    assert version == 1 and size == len(data) and 0 < count < 16
    assert 32 + count * 4 <= len(data)
    parts = []
    for index in range(count):
        offset, = struct.unpack_from("<I", data, 32 + index * 4)
        assert 32 + count * 4 <= offset <= len(data) - 8
        tag, size = struct.unpack_from("<4sI", data, offset)
        assert size <= len(data) - offset - 8
        parts.append((tag.decode("ascii"), size))
    assert any(tag == "DXIL" for tag, _ in parts)
    return parts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=os.environ.get("ZIG", "zig"))
    parser.add_argument("--c-driver", type=Path, default=ROOT / "runtime/c/compiler/build/test_dxil_source_differential")
    parser.add_argument("--evidence-directory", type=Path)
    parser.add_argument("--strict", action="store_true", help="fail on every difference, including documented reference limitations")
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    if not zig or run([zig, "version"]).stdout.strip() != b"0.15.2":
        raise SystemExit("pinned Zig 0.15.2 is required")
    if args.evidence_directory:
        args.evidence_directory.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(prefix="dxil-source-differential-") as directory:
        temp = Path(directory)
        options = temp / "build_options.zig"
        # Proof elimination is deliberately disabled, as in the ordinary build.
        # These unused identity strings make no qualification/provenance claim.
        options.write_text("pub const lean_verified = false;\n" + "".join(
            f'pub const {name}: []const u8 = "unverified-differential";\n'
            for name in ("comparability_obligations_sha256", "lean_toolchain_ref",
                         "lean_extract_program_sha256", "lean_source_tree_sha256",
                         "generated_comparability_contract_sha256", "proof_pattern_spec_sha256")),
            encoding="utf-8")
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".zig",
                                         prefix="dxil_diff_", dir=ZIG_SOURCE) as helper:
            helper.write('''const std = @import("std");
const translation = @import("compiler/wgsl/pipeline/translate_dxil.zig");
const analysis = @import("compiler/wgsl/pipeline/analysis.zig");
pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();
    const args = try std.process.argsAlloc(allocator);
    if (args.len != 2) return error.InvalidArguments;
    const source = try std.fs.cwd().readFileAlloc(allocator, args[1], 1 << 20);
    const output = try allocator.alloc(u8, translation.MAX_OUTPUT);
    var diagnostic = analysis.Diagnostic{};
    const size = translation.translateToDxilWithDiagnostic(allocator, source, output, &diagnostic) catch |err| {
        try std.fs.File.stderr().deprecatedWriter().print("{s} {s}\\n", .{
            @errorName(err), @tagName(diagnostic.lastErrorStage())});
        std.process.exit(1);
    };
    try std.fs.File.stdout().writeAll(output[0..size]);
}
''')
            helper.flush()
            binary = temp / "zig-native-source"
            build = run([zig, "build-exe", "-lc", "-O", "Debug", "--dep", "build_options",
                         "-Mroot=" + helper.name, "-Mbuild_options=" + str(options),
                         "--cache-dir", str(temp / "cache"), "--global-cache-dir", str(temp / "global"),
                         "-femit-bin=" + str(binary)], timeout=120)
            assert build.returncode == 0, build.stderr.decode(errors="replace")
            if args.evidence_directory:
                inputs = sorted((ZIG_SOURCE / "compiler/wgsl").rglob("*.zig"))
                inputs.append(ZIG_SOURCE / "verification/lean_proof.zig")
                source_hash = hashlib.sha256()
                for path in inputs:
                    name = str(path.relative_to(ZIG_SOURCE)).encode()
                    source_hash.update(len(name).to_bytes(8, "little"))
                    source_hash.update(name)
                    source_hash.update(hashlib.sha256(path.read_bytes()).digest())
                provenance = {
                    "scope": "native source translation; structural container, not device semantics",
                    "zig_version": "0.15.2", "lean_verified": False,
                    "zig_source_identity": source_hash.hexdigest(),
                    "zig_binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                    "c_binary_sha256": hashlib.sha256(args.c_driver.read_bytes()).hexdigest(),
                    "harness_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                }
                (args.evidence_directory / "provenance.json").write_text(
                    json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
            for name, source in CASES:
                path = temp / (name + ".wgsl")
                path.write_text(source, encoding="utf-8")
                candidate = run([str(args.c_driver.resolve()), str(path)])
                reference = run([str(binary), str(path)])
                record = {"name": name, "source_sha256": hashlib.sha256(source.encode()).hexdigest()}
                for label, result in (("c", candidate), ("zig", reference)):
                    record[label] = {"exit_code": result.returncode,
                                     "diagnostic": result.stderr.decode(errors="replace").strip(),
                                     "size": len(result.stdout),
                                     "sha256": hashlib.sha256(result.stdout).hexdigest()}
                    if result.returncode == 0:
                        record[label]["parts"] = container(result.stdout)
                    if args.evidence_directory:
                        (args.evidence_directory / f"{name}.{label}.dxbc").write_bytes(result.stdout)
                match = (candidate.returncode == reference.returncode and
                         candidate.returncode in (0, 1) and candidate.stdout == reference.stdout and
                         (candidate.returncode == 0 or candidate.stderr == reference.stderr))
                record["match"] = match
                expected = REFERENCE_DIFFERENCES.get(name)
                observed = (candidate.returncode, record["c"]["diagnostic"],
                            reference.returncode, record["zig"]["diagnostic"])
                known_difference = not match and expected is not None and observed == expected[:4]
                record["known_reference_difference"] = known_difference
                if known_difference:
                    record["reason"] = expected[4]
                records.append(record)
                label = "MATCH" if match else "REFERENCE_DIFFERENCE" if known_difference else "MISMATCH"
                print(f"native source differential: {name}: {label} "
                      f"C={record['c']} Zig={record['zig']}", flush=True)
                if args.evidence_directory:
                    (args.evidence_directory / f"{name}.wgsl").write_text(source, encoding="utf-8")
    if args.evidence_directory:
        (args.evidence_directory / "results.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    differences = sum(record["known_reference_difference"] for record in records)
    failures = sum(not record["match"] and (args.strict or not record["known_reference_difference"]) for record in records)
    matches = sum(record["match"] for record in records)
    print(f"Native DXIL source differential: {matches} exact matches, {differences} documented reference differences, "
          f"{failures} {'strict' if args.strict else 'unexpected'} failures; no device qualification")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
