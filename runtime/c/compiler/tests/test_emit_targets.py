#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual offline target consumers, publication and source attribution."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

OWNER = Path(__file__).resolve().parents[1]
REPO = OWNER.parents[2]
BUILD = Path(os.environ.get("DOE_COMPILER_BUILD", OWNER / "build"))
SOURCE = REPO / "bench/inference-pipeline/kernels/residual.wgsl"


class TargetEmissionTests(unittest.TestCase):
    def run_cli(self, target: str, arguments: list[str]) -> subprocess.CompletedProcess[bytes]:
        return subprocess.run([str(BUILD / f"doe-emit-{target}-c"), *arguments],
                              capture_output=True, timeout=20)

    def test_file_and_stdout_are_identical_and_targets_validate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ("spirv", "msl", "hlsl"):
                with self.subTest(target=target):
                    stdout = self.run_cli(target, ["--shader-path", str(SOURCE)])
                    self.assertEqual(stdout.returncode, 0, stdout.stderr)
                    path = root / target
                    output = self.run_cli(target, ["--shader-path", str(SOURCE), "--out", str(path)])
                    self.assertEqual(output.returncode, 0, output.stderr)
                    self.assertEqual(path.read_bytes(), stdout.stdout)
                    self.assertTrue(stdout.stdout)
                    if target == "spirv":
                        run = subprocess.run([os.environ.get("SPIRV_VAL", "spirv-val"),
                                              "--target-env", "vulkan1.1", str(path)],
                                             capture_output=True, timeout=20)
                        self.assertEqual(run.returncode, 0, run.stderr)
                    else:
                        # Native SDK compilation is retained separately. These
                        # checks catch routing to the other language or SPIR-V.
                        self.assertIn(b"metal_stdlib" if target == "msl" else b"numthreads", stdout.stdout)
                    sentinel = path.read_bytes()
                    refused = self.run_cli(target, ["--shader-path", str(SOURCE), "--out", str(path)])
                    self.assertNotEqual(refused.returncode, 0)
                    self.assertEqual(path.read_bytes(), sentinel)
                    self.assertFalse(list(root.glob("*.tmp.*")))

    def test_retained_optional_graphics_interfaces(self) -> None:
        sources = {
            "clip": """enable clip_distances;
                struct Out { @builtin(position) p:vec4f, @builtin(clip_distances) c:array<f32,5> }
                @vertex fn main()->Out { return Out(vec4f(0,0,0,1),array<f32,5>(1,2,3,4,5)); }""",
            "primitive": """enable primitive_index;
                @fragment fn main(@builtin(primitive_index) p:u32)->@location(0) vec4f { return vec4f(f32(p)); }""",
            "dual": """enable dual_source_blending;
                struct Out { @location(0) @blend_src(0) a:vec4f, @location(0) @blend_src(1) b:vec4f }
                @fragment fn main()->Out { return Out(vec4f(1),vec4f(0.5)); }""",
        }
        markers = {
            "msl": {"clip": b"[[clip_distance]] [5]", "primitive": b"[[primitive_id]]", "dual": b"[[color(0), index(1)]]"},
            "hlsl": {"clip": b"SV_ClipDistance1", "primitive": b"SV_PrimitiveID", "dual": b"SV_Target1"},
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in sources.items():
                source = root / (name + ".wgsl")
                source.write_text(text, encoding="utf-8")
                for target in ("msl", "hlsl", "spirv"):
                    result = self.run_cli(target, ["--shader-path", str(source)])
                    self.assertEqual(result.returncode, 0, (name, target, result.stderr))
                    if target != "spirv":
                        self.assertIn(markers[target][name], result.stdout)
                    else:
                        path = root / (name + ".spv")
                        path.write_bytes(result.stdout)
                        checked = subprocess.run([os.environ.get("SPIRV_VAL", "spirv-val"),
                            "--target-env", "vulkan1.1", str(path)], capture_output=True, timeout=20)
                        self.assertEqual(checked.returncode, 0, checked.stderr)
                source.write_text(text.split(";", 1)[1], encoding="utf-8")
                for target in ("msl", "hlsl", "spirv"):
                    rejected = self.run_cli(target, ["--shader-path", str(source)])
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertEqual(rejected.stdout, b"")

    def test_optional_graphics_semantic_rejections(self) -> None:
        invalid = [
            "enable clip_distances; struct O{@builtin(position)p:vec4f,@builtin(clip_distances)c:array<f32,9>} @vertex fn main()->O{return O();}",
            "enable primitive_index; @vertex fn main(@builtin(primitive_index)p:u32)->@builtin(position)vec4f{return vec4f(f32(p));}",
            "enable dual_source_blending; struct O{@location(0)@blend_src(2)a:vec4f} @fragment fn main()->O{return O();}",
            "enable dual_source_blending; struct O{@location(0)@blend_src(0)a:vec4f} @fragment fn main()->O{return O();}",
            "enable dual_source_blending; struct O{@location(0)@blend_src(0)a:vec4f,@location(0)@blend_src(1)b:vec3f} @fragment fn main()->O{return O();}",
        ]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.wgsl"
            for text in invalid:
                source.write_text(text, encoding="utf-8")
                for target in ("msl", "hlsl", "spirv"):
                    result = self.run_cli(target, ["--shader-path", str(source)])
                    self.assertNotEqual(result.returncode, 0, text)
                    self.assertEqual(result.stdout, b"")

    def test_external_plane_zero_and_base_clamp(self) -> None:
        source_text = """
            @group(0) @binding(0) var image:texture_external;
            @group(0) @binding(1) var sample_state:sampler;
            @group(0) @binding(2) var<storage,read_write> output:array<vec4f>;
            fn sample_external(t:texture_external,s:sampler,p:vec2f)->vec4f {
                return textureSampleBaseClampToEdge(t,s,p); }
            @compute @workgroup_size(1) fn main(){
                output[0]=sample_external(image,sample_state,vec2f(-1.0,2.0));
                output[1]=textureLoad(image,vec2i(0));
                output[2]=vec4f(vec2f(textureDimensions(image)),0,1);
            }
        """
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            source=root / "external.wgsl"
            for external in (True,False):
                text=source_text if external else source_text.replace("texture_external","texture_2d<f32>").replace("textureLoad(image,vec2i(0))","textureLoad(image,vec2i(0),0)")
                source.write_text(text,encoding="utf-8")
                for target in ("msl","hlsl","spirv"):
                    result=self.run_cli(target,["--shader-path",str(source)])
                    self.assertEqual(result.returncode,0,(target,result.stderr))
                    if target=="spirv":
                        binary=root / "external.spv"
                        binary.write_bytes(result.stdout)
                        checked=subprocess.run([os.environ.get("SPIRV_VAL","spirv-val"),"--target-env","vulkan1.1",str(binary)],capture_output=True,timeout=20)
                        self.assertEqual(checked.returncode,0,checked.stderr)
                    else:
                        self.assertIn(b"0.5f / float2(image_x,image_y)",result.stdout)
                        self.assertIn(b"level(0.0f)" if target=="msl" else b"SampleLevel(",result.stdout)
            mixed = """
                @group(0)@binding(0)var e:texture_external;
                @group(0)@binding(1)var t:texture_2d<f32>;
                @group(0)@binding(2)var s:sampler;
                fn a(x:texture_external,y:sampler)->vec4f{return textureSampleBaseClampToEdge(x,y,vec2f(0));}
                fn b(x:texture_2d<f32>,y:sampler)->vec4f{return textureSampleBaseClampToEdge(x,y,vec2f(1));}
                @fragment fn main()->@location(0)vec4f{return a(e,s)+b(t,s);}
            """
            source.write_text(mixed,encoding="utf-8")
            for target in ("msl","hlsl","spirv"):
                result=self.run_cli(target,["--shader-path",str(source)])
                self.assertEqual(result.returncode,0,(target,result.stderr))
                if target=="spirv":
                    binary=root / "mixed.spv"
                    binary.write_bytes(result.stdout)
                    checked=subprocess.run([os.environ.get("SPIRV_VAL","spirv-val"),"--target-env","vulkan1.1",str(binary)],capture_output=True,timeout=20)
                    self.assertEqual(checked.returncode,0,checked.stderr)
            prefix="@group(0)@binding(0)var t:texture_external; @group(0)@binding(1)var s:sampler; "
            for expression in ("textureSample(t,s,vec2f(0))","textureSampleLevel(t,s,vec2f(0),0)",
                               "textureNumLevels(t)","textureLoad(t,vec2i(0),0)","textureDimensions(t,0)"):
                source.write_text(prefix+"@fragment fn main(){let x="+expression+";}",encoding="utf-8")
                for target in ("msl","hlsl","spirv"):
                    rejected=self.run_cli(target,["--shader-path",str(source)])
                    self.assertNotEqual(rejected.returncode,0,expression)
                    self.assertEqual(rejected.stdout,b"")

    def test_invalid_source_reports_location_without_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "invalid.wgsl"
            source.write_text("\n@compute @workgroup_size(1) fn main() { let x: u32 = false; }\n", encoding="utf-8")
            for target in ("spirv", "msl", "hlsl"):
                path = root / target
                result = self.run_cli(target, ["--shader-path", str(source), "--out", str(path)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b":2:", result.stderr)
                self.assertFalse(path.exists())
                self.assertEqual(result.stdout, b"")
            self.assertEqual([p.name for p in root.iterdir()], ["invalid.wgsl"])

    def test_empty_valid_module_and_invalid_options(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "empty.wgsl"
            source.write_text("// entryless module\n", encoding="utf-8")
            for target in ("msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b"")
                help_result = self.run_cli(target, ["--help"])
                self.assertEqual(help_result.returncode, 0)
                for arguments in (["--target", "dxil"], ["--target", "msl", "--target", "hlsl"],
                                  ["--reflection", "unused.json"]):
                    result = self.run_cli(target, ["--shader-path", str(source), *arguments])
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, b"")

    def test_text_job_allocation_failures_publish_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ("msl", "hlsl"):
                output = root / target
                for call in range(1, 17):
                    env = dict(os.environ, DOE_CLI_FAULT="malloc", DOE_CLI_FAULT_CALL=str(call))
                    result = subprocess.run([str(BUILD / "test_cli_faults"), "--target", target,
                                             "--shader-path", str(SOURCE), "--out", str(output)],
                                            capture_output=True, timeout=20, env=env)
                    if result.returncode == 0:
                        self.assertGreater(call, 1)
                        self.assertTrue(output.read_bytes())
                        output.unlink()
                        break
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn(b"allocation", result.stderr)
                    self.assertEqual(list(root.iterdir()), [])
                else:
                    self.fail("allocation sweep did not reach success")

    def test_retained_source_size_boundary_and_diagnostic_offset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "large.wgsl"
            limit = 2 * 1024 * 1024
            shader = b"\n@compute @workgroup_size(1) fn main() {}\n"
            source.write_bytes(b"//" + b" " * (limit - len(shader) - 2) + shader)
            for target in ("spirv", "msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout)
            source.write_bytes(source.read_bytes() + b" ")
            for target in ("spirv", "msl", "hlsl"):
                output = root / target
                result = self.run_cli(target, ["--shader-path", str(source), "--out", str(output)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"2 MiB", result.stderr)
                self.assertEqual(result.stdout, b"")
                self.assertFalse(output.exists())
            prefix = b"//" + b" " * 70000 + b"\n"
            source.write_bytes(prefix + b"@compute @workgroup_size(1) fn main() { let x: u32 = false; }\n")
            for target in ("spirv", "msl", "hlsl"):
                result = self.run_cli(target, ["--shader-path", str(source)])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b":2:", result.stderr)
                offset = int(result.stderr.split(b"offset=", 1)[1].split(b":", 1)[0])
                self.assertGreater(offset, 70000)
                self.assertEqual(result.stdout, b"")

    def test_runtime_overrides_preserve_all_entries_and_exact_workgroup_sizes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bundle.wgsl"
            source.write_text("""override n: u32 = 2;
@id(7) override m: u32 = 3;
override shade: f32;
override unused: u32;
@compute @workgroup_size(n) fn first() {}
@fragment fn color() -> @location(0) vec4f { return vec4f(shade); }
@compute @workgroup_size(m) fn second() {}
""", encoding="utf-8")
            base = ["--shader-path", str(source), "--mode", "vulkan-compute-runtime"]
            result = self.run_cli("spirv", [*base, "--overrides", "n=4,7=5", "--overrides", "shade=0.5"])
            self.assertEqual(result.returncode, 0, result.stderr)
            artifact = root / "bundle.spv"
            artifact.write_bytes(result.stdout)
            validated = subprocess.run([os.environ.get("SPIRV_VAL", "spirv-val"),
                                        "--target-env", "vulkan1.1", str(artifact)],
                                       capture_output=True, timeout=20)
            self.assertEqual(validated.returncode, 0, validated.stderr)
            words = struct.unpack(f"<{len(result.stdout) // 4}I", result.stdout)
            entries = []
            sizes = {}
            offset = 5
            while offset < len(words):
                count, opcode = words[offset] >> 16, words[offset] & 0xffff
                self.assertGreater(count, 0)
                self.assertLessEqual(offset + count, len(words))
                if opcode == 15:
                    name = struct.pack(f"<{count - 3}I", *words[offset + 3:offset + count]).split(b"\0")[0]
                    entries.append((words[offset + 1], words[offset + 2], name))
                if opcode == 16 and words[offset + 2] == 17:
                    sizes[words[offset + 1]] = words[offset + 3:offset + 6]
                offset += count
            self.assertEqual([(stage, name) for stage, _, name in entries],
                             [(5, b"first"), (4, b"color"), (5, b"second")])
            self.assertEqual(sizes[entries[0][1]], (4, 1, 1))
            self.assertEqual(sizes[entries[2][1]], (5, 1, 1))
            defaulted = self.run_cli("spirv", [*base, "--overrides", "shade=0.5"])
            self.assertEqual(defaulted.returncode, 0, defaulted.stderr)
            output = root / "unpublished.spv"
            for values in ("n=4,7=5", "shade=nan", "shade=inf", "shade=0.5,shade=1",
                           "shade=0.5,", "=1", "shade=", "shade=0.5,n=-1",
                           "shade=0.5,m=4", "shade=0.5,unknown=1", "shade= 0.5"):
                rejected = self.run_cli("spirv", [*base, "--out", str(output), "--overrides", values])
                self.assertNotEqual(rejected.returncode, 0, (values, rejected.stderr))
                self.assertFalse(output.exists())
                self.assertEqual(rejected.stdout, b"")
            repeated = self.run_cli("spirv", [*base, "--overrides", "shade=0.5", "--overrides", "shade=1"])
            self.assertEqual(repeated.returncode, 2)
            wrong_mode = self.run_cli("spirv", ["--shader-path", str(source), "--overrides", "shade=0.5"])
            self.assertEqual(wrong_mode.returncode, 2)

    def test_runtime_override_limit_and_allocation_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "overrides.wgsl"
            source.write_text("\n".join(f"override v{i}: u32;" for i in range(64)) +
                              "\n@compute @workgroup_size(1) fn main() {}\n", encoding="utf-8")
            values = ",".join(f"v{i}={i}" for i in range(64))
            base = ["--shader-path", str(source), "--mode", "vulkan-compute-runtime", "--overrides", values]
            result = self.run_cli("spirv", base)
            self.assertEqual(result.returncode, 0, result.stderr)
            excessive = self.run_cli("spirv", [*base, "--overrides", "extra=1"])
            self.assertEqual(excessive.returncode, 2)
            output = root / "output.spv"
            for call in range(1, 17):
                env = dict(os.environ, DOE_CLI_FAULT="malloc", DOE_CLI_FAULT_CALL=str(call))
                result = subprocess.run([str(BUILD / "test_cli_faults"), *base, "--out", str(output)],
                                        capture_output=True, timeout=20, env=env)
                if result.returncode == 0:
                    self.assertGreater(call, 1)
                    self.assertTrue(output.read_bytes())
                    output.unlink()
                    break
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(b"allocation", result.stderr)
                self.assertEqual([p.name for p in root.iterdir()], ["overrides.wgsl"])
            else:
                self.fail("allocation sweep did not reach success")



if __name__ == "__main__":
    unittest.main()
