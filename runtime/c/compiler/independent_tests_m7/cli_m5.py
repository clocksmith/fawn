"""Independent M5 CLI JSON, mode, publication and failure-boundary checks."""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import subprocess

MODES = {"binary": None, "legacy": "--reflection", "v2": "--reflection-v2"}
OPTIONS = ("--shader-path", "--out", "--reflection", "--reflection-v2")
CFLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
          "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
          "-Wmissing-prototypes", "-Wswitch-enum", "-Werror"]
WRAPS = ("main", "malloc", "calloc", "realloc", "free", "write", "read",
         "close", "link", "mkstemp", "lstat", "doe_compiler_workspace_requirements",
         "doe_compiler_compile", "doe_compiler_compile_v2")
STORAGE = ("@group(1) @binding(2) var<storage, read> src: array<u32>;\n"
           "@group(1) @binding(3) var<storage, read_write> dst: array<u32>;\n"
           "@compute @workgroup_size(64)\n"
           "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
           " dst[gid.x] = src[gid.x] + 7u;\n}\n").encode()

# Generated only from retained M4 baseline CLI 94448360c2409bb37a9c0c6343d8f10a
# 20a76410c7353e9e39b4a024ed4d3154, never from the M5 candidate.
LEGACY_GOLDEN = {'21f18f6bad8c79bd9246f27b0eb25b714f666b7c8ffbd4f56ae0841589d4cab3': {'json': '{\n'
                                                                              '  '
                                                                              '"entry_point": '
                                                                              '"main",\n'
                                                                              '  '
                                                                              '"workgroup_size": '
                                                                              '[64, '
                                                                              '1, '
                                                                              '1],\n'
                                                                              '  '
                                                                              '"binding_count": '
                                                                              '1,\n'
                                                                              '  '
                                                                              '"bindings": '
                                                                              '[\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"data", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '0, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read_write", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true}\n'
                                                                              '  '
                                                                              ']\n'
                                                                              '}\n',
                                                                      'spirv_sha256': 'aa2f895d7a53173405d803080b172001faad59232dbfe12a7f45395c1f24b095'},
 '75ba7169ecc1af5f1ee94a94e775c3229fb833e61c0cb16af36a3fc045a31c62': {'json': '{\n'
                                                                              '  '
                                                                              '"entry_point": '
                                                                              '"main",\n'
                                                                              '  '
                                                                              '"workgroup_size": '
                                                                              '[256, '
                                                                              '1, '
                                                                              '1],\n'
                                                                              '  '
                                                                              '"binding_count": '
                                                                              '3,\n'
                                                                              '  '
                                                                              '"bindings": '
                                                                              '[\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"a", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '0, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true},\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"b", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '1, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true},\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"out", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '2, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read_write", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true}\n'
                                                                              '  '
                                                                              ']\n'
                                                                              '}\n',
                                                                      'spirv_sha256': '49e4f4a3015051644884a5c3a7345d3e0914294d5d78040282ef2028a4c6043e'},
 'a79c6bb744a3c13f734f1e17a2f9f617c05aac7ae3d64f67c9cc98945f459acc': {'json': '{\n'
                                                                              '  '
                                                                              '"entry_point": '
                                                                              '"main",\n'
                                                                              '  '
                                                                              '"workgroup_size": '
                                                                              '[256, '
                                                                              '1, '
                                                                              '1],\n'
                                                                              '  '
                                                                              '"binding_count": '
                                                                              '2,\n'
                                                                              '  '
                                                                              '"bindings": '
                                                                              '[\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"input", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '0, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true},\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"output", '
                                                                              '"group": '
                                                                              '0, '
                                                                              '"binding": '
                                                                              '1, '
                                                                              '"scalar_type": '
                                                                              '"f32", '
                                                                              '"access": '
                                                                              '"read_write", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true}\n'
                                                                              '  '
                                                                              ']\n'
                                                                              '}\n',
                                                                      'spirv_sha256': 'd30983bb9bf9c28dd3c601c42127df23e2c68334a90eed55664b60a7c40ec2c5'},
 'baf23bf6df9dcd9abb14edbc4919a31f96e49c8d5da2f28fe0fd8ab2c6470112': {'json': '{\n'
                                                                              '  '
                                                                              '"entry_point": '
                                                                              '"main",\n'
                                                                              '  '
                                                                              '"workgroup_size": '
                                                                              '[64, '
                                                                              '1, '
                                                                              '1],\n'
                                                                              '  '
                                                                              '"binding_count": '
                                                                              '2,\n'
                                                                              '  '
                                                                              '"bindings": '
                                                                              '[\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"src", '
                                                                              '"group": '
                                                                              '1, '
                                                                              '"binding": '
                                                                              '2, '
                                                                              '"scalar_type": '
                                                                              '"u32", '
                                                                              '"access": '
                                                                              '"read", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true},\n'
                                                                              '    '
                                                                              '{"name": '
                                                                              '"dst", '
                                                                              '"group": '
                                                                              '1, '
                                                                              '"binding": '
                                                                              '3, '
                                                                              '"scalar_type": '
                                                                              '"u32", '
                                                                              '"access": '
                                                                              '"read_write", '
                                                                              '"element_stride": '
                                                                              '4, '
                                                                              '"runtime_sized": '
                                                                              'true}\n'
                                                                              '  '
                                                                              ']\n'
                                                                              '}\n',
                                                                      'spirv_sha256': '4cc4f5c93dfd36491b26c68baf31e60201fe490042befc0013cc75e1338acc52'}}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tree(root: Path) -> dict[str, object]:
    result = {}
    for path in sorted(root.rglob("*")):
        name = str(path.relative_to(root))
        if path.is_symlink():
            result[name] = ["symlink", os.readlink(path)]
        elif path.is_file():
            result[name] = ["file", sha(path.read_bytes())]
        elif path.is_dir():
            result[name] = ["directory"]
    return result


def binding(name: str, group: int, number: int, scalar: str | None,
            access: str = "read", count: int = 0, index: int = 0) -> dict:
    is_uniform = scalar is None
    return {"name": name, "group": group, "binding": number,
            "kind": "uniform_buffer" if is_uniform else "storage_buffer",
            "access": access, "scalar": scalar,
            "element_stride": 0 if is_uniform else 4,
            "runtime_sized": 0 if is_uniform else 1,
            "minimum_binding_size_bytes": count * 4 if is_uniform else 4,
            "type_alignment_bytes": 4,
            "address_space_alignment_bytes": 16 if is_uniform else 4,
            "struct_index": index if is_uniform else None}


def structure(name: str, members: list[tuple[str, str]]) -> dict:
    return {"name": name, "member_count": len(members),
            "size_bytes": 4 * len(members), "alignment_bytes": 4,
            "members": [{"name": n, "scalar": scalar, "offset_bytes": i * 4,
                         "size_bytes": 4, "alignment_bytes": 4}
                        for i, (n, scalar) in enumerate(members)]}


def reflection(bindings: list[dict], structs: list[dict], workgroup: int = 1,
               entry: str = "main") -> dict:
    return {"schema_version": 2, "entry_point": entry,
            "workgroup_size": [workgroup, 1, 1],
            "binding_count": len(bindings), "bindings": bindings,
            "struct_count": len(structs), "structs": structs}


def fixtures(repo: Path) -> list[tuple[str, bytes, dict]]:
    cases = [("storage", STORAGE, reflection([
        binding("src", 1, 2, "u32"),
        binding("dst", 1, 3, "u32", "read_write")], [], 64))]
    for count in (1, 3, 4, 5, 8):
        members = [(f"member{i}", "u32" if i % 2 == 0 else "f32")
                   for i in range(count)]
        source = "struct Mixed { " + ", ".join(
            name + ": " + scalar for name, scalar in members) + ", }\n"
        source += ("@group(3) @binding(7) var<uniform> uniform_data: Mixed;\n"
                   "@compute @workgroup_size(1)\n"
                   "fn main(@builtin(global_invocation_id) gid: vec3u) { return; }\n")
        cases.append((f"uniform_{count}", source.encode(), reflection([
            binding("uniform_data", 3, 7, None, count=count)],
            [structure("Mixed", members)])))
    source = ("@group(0) @binding(7) var<uniform> later: Shared;\n"
              "struct Unused { again: f32, }\n"
              "struct Shared { again: u32, value: f32, tail: u32, }\n"
              "@group(3) @binding(7) var<uniform> first: Shared;\n"
              "@group(2) @binding(4) var<storage, read> data: array<f32>;\n"
              "@compute @workgroup_size(2)\n"
              "fn main(@builtin(global_invocation_id) gid: vec3u) { return; }\n")
    cases.append(("shared_forward_unused", source.encode(), reflection([
        binding("later", 0, 7, None, count=3, index=1),
        binding("first", 3, 7, None, count=3, index=1),
        binding("data", 2, 4, "f32")], [structure("Unused", [("again", "f32")]),
        structure("Shared", [("again", "u32"), ("value", "f32"),
                             ("tail", "u32")])], 2)))
    source_parts = []
    structures = []
    bindings = []
    for i in range(8):
        name = f"Type{i}_" + "t" * 57
        members = [(f"member{j}_" + "m" * 55, "u32" if j % 2 == 0 else "f32")
                   for j in range(8)]
        source_parts.append("struct " + name + " { " + ", ".join(
            n + ": " + s for n, s in members) + ", }\n")
        structures.append(structure(name, members))
        resource = f"resource{i}_" + "r" * 53
        source_parts.append(f"@group({i % 4}) @binding({i + 10}) var<uniform> "
                            f"{resource}: {name};\n")
        bindings.append(binding(resource, i % 4, i + 10, None, count=8, index=i))
    source_parts.append("@compute @workgroup_size(1)\n"
                        "fn main(@builtin(global_invocation_id) gid: vec3u) { return; }\n")
    cases.append(("capacity_names_63", "".join(source_parts).encode(),
                  reflection(bindings, structures)))
    package = repo / "bench/kernels/package_vector_scale_add.wgsl"
    cases.append(("unchanged_package", package.read_bytes(), reflection([
        binding("params", 0, 0, None, count=4),
        binding("input_values", 0, 1, "f32"),
        binding("output_values", 0, 2, "f32", "read_write")],
        [structure("Params", [("count", "u32"), ("_pad0", "u32"),
                              ("_pad1", "u32"), ("_pad2", "u32")])], 64)))
    return cases


class CheckCli:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.records: list[dict] = []
        self.commands: list[dict] = []
        self.counter = 0

    def directory(self, label: str) -> Path:
        self.counter += 1
        path = self.args.evidence / f"{self.counter:04d}_{label}"
        path.mkdir()
        return path

    def record(self, case: str, errors: list[str], **details: object) -> None:
        stderr = str(details.get("stderr", ""))
        if any(marker in stderr for marker in (
                "AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")):
            errors = [*errors, "Sanitizer diagnostic in child process"]
        if isinstance(details.get("returncode"), int) and details["returncode"] < 0:
            errors = [*errors, "Child terminated by signal"]
        row = {"case": case, "passed": not errors, "errors": errors, **details}
        self.records.append(row)
        # Keep the exact first failure even if a later evaluator repair reruns.
        with (self.args.evidence / "checks.jsonl").open("a", encoding="utf-8") as file:
            file.write(json.dumps(row, sort_keys=True) + "\n")
        if errors:
            print(json.dumps(row, sort_keys=True), flush=True)

    def invoke(self, cli: Path, parts: list[str | Path], directory: Path,
               operation: str | None = None, call: int = 1) -> tuple[subprocess.CompletedProcess, dict | None]:
        command = [str(cli), *map(str, parts)]
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        if operation:
            environment.update(M5_FAULT_OPERATION=operation,
                               M5_FAULT_CALL=str(call))
        else:
            environment.pop("M5_FAULT_OPERATION", None)
            environment.pop("M5_FAULT_CALL", None)
        run = subprocess.run(command, capture_output=True, text=True,
                             timeout=30, cwd=directory, env=environment, check=False)
        trace = None
        for line in run.stderr.splitlines():
            if line.startswith("M5_TRACE "):
                trace = json.loads(line[len("M5_TRACE "):])
        self.commands.append({"command": command, "cwd": str(directory),
                              "fault_operation": operation, "fault_call": call,
                              "returncode": run.returncode,
                              "stdout": run.stdout, "stderr": run.stderr})
        return run, trace

    def parts(self, directory: Path, mode: str) -> list[str | Path]:
        result: list[str | Path] = ["--shader-path", directory / "source.wgsl",
                                  "--out", directory / "module.spv"]
        if MODES[mode]:
            result += [MODES[mode], directory / "reflection.json"]
        return result

    def validate(self, path: Path) -> list[str]:
        if not path.is_file():
            return ["No binary output"]
        command = [str(self.args.validator), "--target-env", "vulkan1.1", str(path)]
        run = subprocess.run(command, capture_output=True, text=True,
                             timeout=30, check=False)
        self.commands.append({"command": command, "returncode": run.returncode,
                              "stdout": run.stdout, "stderr": run.stderr})
        return [] if not run.returncode else ["Vulkan1.1: " + run.stderr]

    def positive(self, cli: Path, label: str) -> None:
        for name, source, expected in fixtures(self.args.repo):
            binary = None
            deterministic = None
            for mode in MODES:
                d = self.directory(label + "_" + name + "_" + mode)
                (d / "source.wgsl").write_bytes(source)
                before = tree(d)
                run, trace = self.invoke(cli, self.parts(d, mode), d)
                errors = []
                is_legacy_reject = mode == "legacy" and expected["struct_count"] > 0
                if is_legacy_reject:
                    if not run.returncode or tree(d) != before:
                        errors.append("Legacy ABI1 admitted uniform or published artifacts")
                elif run.returncode:
                    errors.append("Expected success: " + run.stderr)
                else:
                    errors += self.validate(d / "module.spv")
                    raw = (d / "module.spv").read_bytes()
                    if binary is not None and raw != binary:
                        errors.append("Binary differs between output modes")
                    binary = raw
                    if mode == "v2":
                        actual = json.loads((d / "reflection.json").read_text(encoding="utf-8"))
                        # Python equality otherwise considers True equal to 1.
                        if actual != expected:
                            errors.append("Exact v2 schema/value mismatch")
                        for item in actual.get("bindings", []):
                            if type(item.get("runtime_sized")) is not int:
                                errors.append("runtime_sized is not numeric 0/1")
                        deterministic = (d / "reflection.json").read_bytes()
                    wanted = {"source.wgsl", "module.spv"}
                    if mode != "binary":
                        wanted.add("reflection.json")
                    if {p.name for p in d.iterdir()} != wanted:
                        errors.append("Unexpected JSON/default sidecar/temporary")
                self.record(label + "_positive_" + name + "_" + mode, errors,
                            returncode=run.returncode, stderr=run.stderr, trace=trace,
                            source_sha256=sha(source))
            if deterministic is not None:
                d = self.directory(label + "_deterministic_" + name)
                (d / "source.wgsl").write_bytes(source)
                run, _ = self.invoke(cli, self.parts(d, "v2"), d)
                errors = []
                if run.returncode or (d / "reflection.json").read_bytes() != deterministic:
                    errors.append("V2 output not byte deterministic")
                self.record(label + "_deterministic_" + name, errors)

    def legacy_baseline(self) -> None:
        sources = [("synthetic", STORAGE)]
        ledger = json.loads((self.args.repo / "docs/c-migration/corpus-coverage-m4.json")
                            .read_text(encoding="utf-8"))
        sources += [(Path(row["source"]).stem, (self.args.repo / row["source"]).read_bytes())
                    for row in ledger["records"] if row["admitted"]]
        for name, source in sources:
            expected = LEGACY_GOLDEN.get(sha(source))
            if expected is None:
                self.record("legacy_bytes_" + name, ["No source-bound frozen golden"])
                continue
            errors = []
            binaries = [("candidate", self.args.cli)]
            if self.args.baseline is not None:
                binaries.insert(0, ("baseline", self.args.baseline))
            for label, cli in binaries:
                d = self.directory("legacy_" + label + "_" + name)
                (d / "source.wgsl").write_bytes(source)
                run, _ = self.invoke(cli, self.parts(d, "legacy"), d)
                if run.returncode:
                    errors.append(label + " rejected retained source: " + run.stderr)
                else:
                    if sha((d / "module.spv").read_bytes()) != expected["spirv_sha256"]:
                        errors.append(label + " changed legacy SPIR-V bytes")
                    if (d / "reflection.json").read_bytes() != expected["json"].encode():
                        errors.append(label + " changed legacy JSON bytes")
            self.record("legacy_bytes_" + name, errors, source_sha256=sha(source),
                        baseline_comparison=self.args.baseline is not None,
                        frozen_golden_comparison=True)

    def arguments(self, cli: Path, label: str) -> None:
        for mode in MODES:
            mutations = ["no_args", "missing_input", "missing_output", "unknown",
                         "positional", "delimiter", "mutual"]
            mutations += [kind + "_" + option[2:] for kind in
                          ("empty", "missing", "duplicate") for option in OPTIONS]
            for name in mutations:
                d = self.directory(label + "_arg_" + mode + "_" + name)
                (d / "source.wgsl").write_bytes(STORAGE)
                parts = list(map(str, self.parts(d, mode)))
                if name == "no_args":
                    parts = []
                elif name == "missing_input":
                    parts = parts[2:]
                elif name == "missing_output":
                    parts = parts[:2] + parts[4:]
                elif name == "unknown":
                    parts += ["--bogus", "value"]
                elif name == "positional":
                    parts += ["extra"]
                elif name == "delimiter":
                    parts += ["--"]
                elif name == "mutual":
                    parts = list(map(str, self.parts(d, "binary"))) + [
                        "--reflection", str(d / "old.json"),
                        "--reflection-v2", str(d / "new.json")]
                else:
                    kind, option_name = name.split("_", 1)
                    option = "--" + option_name
                    if kind == "missing":
                        parts += [option]
                    else:
                        if option not in parts:
                            parts += [option, str(d / "extra.json")]
                        if kind == "empty":
                            parts[parts.index(option) + 1] = ""
                        else:
                            parts += [option, str(d / "different")]
                before = tree(d)
                run, _ = self.invoke(cli, parts, d)
                errors = []
                if not run.returncode or (not name.startswith("empty_") and run.returncode != 2):
                    errors.append("Incorrect argument rejection status")
                if tree(d) != before:
                    errors.append("Argument failure modified filesystem")
                self.record(label + "_arg_" + mode + "_" + name, errors,
                            returncode=run.returncode, stderr=run.stderr)
            for target, following in itertools.product(OPTIONS, repeat=2):
                d = self.directory(label + "_token_" + mode)
                (d / "source.wgsl").write_bytes(STORAGE)
                parts = list(map(str, self.parts(d, mode)))
                if target not in parts:
                    parts += [target, str(d / "extra.json")]
                parts[parts.index(target) + 1] = following
                before = tree(d)
                run, _ = self.invoke(cli, parts, d)
                errors = [] if run.returncode == 2 and tree(d) == before else [
                    "Recognized option used as value or filesystem changed"]
                self.record(label + "_token_" + mode + "_" + target + "_" + following,
                            errors, returncode=run.returncode, stderr=run.stderr)
            pairs = [self.parts(Path("."), mode)[i:i + 2]
                     for i in range(0, len(self.parts(Path("."), mode)), 2)]
            for order in itertools.permutations(range(len(pairs))):
                d = self.directory(label + "_permutation_" + mode)
                (d / "source.wgsl").write_bytes(STORAGE)
                parts = [str(x) for i in order for x in pairs[i]]
                run, _ = self.invoke(cli, parts, d)
                self.record(label + "_permutation_" + mode + "_" + str(order),
                            [] if run.returncode == 0 else [run.stderr])

    def paths(self, cli: Path, label: str) -> None:
        for mode in MODES:
            names = ["existing_binary", "directory_binary", "symlink_binary",
                     "dangling_binary", "hardlink_binary", "binary_is_source",
                     "missing_binary_dir", "empty_source", "nul_source",
                     "oversize_source", "bad_source", "source_symlink", "unicode_paths",
                     "long_input", "long_binary", "missing_input", "directory_input",
                     "source_exact_limit", "flaglike_paths", "escaped_option_paths"]
            if mode != "binary":
                names += ["existing_json", "directory_json", "symlink_json",
                          "dangling_json", "json_is_source", "same_destinations",
                          "aliased_destinations", "missing_json_dir", "long_json"]
            for name in names:
                d = self.directory(label + "_path_" + mode + "_" + name)
                source, binary, sidecar = d / "source.wgsl", d / "module.spv", d / "reflection.json"
                source.write_bytes(STORAGE)
                is_success = name in {"source_symlink", "unicode_paths", "source_exact_limit",
                                      "flaglike_paths", "escaped_option_paths"}
                if name == "existing_binary": binary.write_bytes(b"KEEP\x00\xff")
                elif name == "directory_binary": binary.mkdir()
                elif name == "symlink_binary": binary.symlink_to(source)
                elif name == "dangling_binary": binary.symlink_to(d / "absent")
                elif name == "hardlink_binary": os.link(source, binary)
                elif name == "binary_is_source": binary = source
                elif name == "missing_binary_dir": binary = d / "absent" / "module.spv"
                elif name == "empty_source": source.write_bytes(b"")
                elif name == "nul_source": source.write_bytes(STORAGE + b"\x00")
                elif name == "oversize_source": source.write_bytes(STORAGE + b" " * 65537)
                elif name == "bad_source": source.write_bytes(b"invalid wgsl")
                elif name == "source_symlink":
                    linked = d / "link.wgsl"; linked.symlink_to(source); source = linked
                elif name == "unicode_paths":
                    binary = d / "output ü space.spv"; sidecar = d / "metadata ü space.json"
                elif name == "long_input": source = d / ("x" * 5000)
                elif name == "long_binary": binary = d / ("x" * 5000)
                elif name == "long_json": sidecar = d / ("x" * 5000)
                elif name == "missing_input": source = d / "absent.wgsl"
                elif name == "directory_input":
                    source = d / "input_directory"; source.mkdir()
                elif name == "source_exact_limit":
                    source.write_bytes(STORAGE + b" " * (65536 - len(STORAGE)))
                elif name == "flaglike_paths":
                    binary = d / "--unknown"; sidecar = d / "--help"
                elif name == "escaped_option_paths":
                    binary = d / "--reflection-v2"; sidecar = d / "--out"
                elif name == "existing_json": sidecar.write_bytes(b"KEEP\x00\xff")
                elif name == "directory_json": sidecar.mkdir()
                elif name == "symlink_json": sidecar.symlink_to(source)
                elif name == "dangling_json": sidecar.symlink_to(d / "absent")
                elif name == "json_is_source": sidecar = source
                elif name == "same_destinations": sidecar = binary
                elif name == "aliased_destinations":
                    (d / "alias").symlink_to(d, target_is_directory=True)
                    sidecar = d / "alias" / "module.spv"
                elif name == "missing_json_dir": sidecar = d / "absent" / "reflection.json"
                parts = ["--shader-path", source, "--out", binary]
                if MODES[mode]: parts += [MODES[mode], sidecar]
                before = tree(d)
                run, _ = self.invoke(cli, parts, d)
                errors = []
                if is_success:
                    wanted = dict(before)
                    for path in [binary] + ([sidecar] if mode != "binary" else []):
                        if path.is_file():
                            wanted[str(path.relative_to(d))] = ["file", sha(path.read_bytes())]
                        else:
                            errors.append("Missing expected output")
                    if run.returncode or tree(d) != wanted:
                        errors.append("Positive path failed or left residue")
                elif not run.returncode or tree(d) != before:
                    errors.append("No-clobber/rollback/source preservation failed")
                self.record(label + "_path_" + mode + "_" + name, errors,
                            returncode=run.returncode, stderr=run.stderr)

    def faults(self, cli: Path, label: str) -> None:
        for mode in MODES:
            d = self.directory(label + "_trace_" + mode)
            (d / "source.wgsl").write_bytes(STORAGE)
            run, counts = self.invoke(cli, self.parts(d, mode), d)
            errors = []
            if run.returncode or counts is None:
                self.record(label + "_trace_" + mode, ["Wrapped normal execution failed"],
                            stderr=run.stderr)
                continue
            if counts["live"]:
                errors.append("Owned allocations not freed")
            if mode == "legacy":
                if counts["abi1_calls"] != 1 or counts["abi2_calls"] or counts["queries"]:
                    errors.append("Legacy mode not exclusively ABI1")
            elif counts["abi2_calls"] != 1 or counts["abi1_calls"] or counts["queries"] != 1:
                errors.append("Binary/v2 mode not exclusively queried ABI2")
            expected_temps = 1 if mode == "binary" else 2
            if counts["temps"] != expected_temps or counts["links"] != expected_temps or counts["lstats"] != expected_temps:
                errors.append("Optional JSON temporary/publication invariant failed")
            if mode == "binary" and 32768 in counts["sizes"]:
                errors.append("Binary-only allocated v2 JSON storage")
            if mode == "v2" and counts["sizes"].count(32768) != 1:
                errors.append("V2 JSON checked capacity allocation is not 32768")
            self.record(label + "_trace_" + mode, errors, trace=counts)
            normal_binary = (d / "module.spv").read_bytes()
            normal_json = (d / "reflection.json").read_bytes() if mode != "binary" else None
            matrix = [(op, i) for op, count_key in (
                ("allocation", "allocations"), ("write", "writes"),
                ("close", "closes"), ("link", "links"),
                ("publish_collision", "links"),
                ("mkstemp", "temps"), ("unowned_temp", "temps"))
                for i in range(1, counts[count_key] + 1)]
            matrix += [("read", 1), ("read_eintr", 1), ("read_short", 1)]
            matrix += [(op, i) for op in ("write_eintr", "write_short")
                       for i in range(1, counts["writes"] + 1)]
            if mode != "legacy": matrix += [("query", 1)]
            for operation, call in matrix:
                d = self.directory(label + "_fault_" + mode + "_" + operation + "_" + str(call))
                (d / "source.wgsl").write_bytes(STORAGE)
                before = tree(d)
                run, trace = self.invoke(cli, self.parts(d, mode), d, operation, call)
                errors = []
                success = operation in {"read_eintr", "read_short", "write_eintr", "write_short"}
                if trace is None or trace["faults"] != 1:
                    errors.append("Fault injection did not execute exactly once")
                if trace and trace["live"]:
                    errors.append("Failure leaked owned allocation")
                if operation == "query" and trace and trace["allocations"]:
                    errors.append("Query failure performed allocation")
                if success:
                    wanted = dict(before)
                    if run.returncode:
                        errors.append("Retryable I/O did not complete")
                    for name, expected in (("module.spv", normal_binary),
                                           ("reflection.json", normal_json)):
                        if expected is not None:
                            path = d / name
                            if not path.is_file() or path.read_bytes() != expected:
                                errors.append("Retry changed or omitted " + name)
                            wanted[name] = ["file", sha(expected)]
                    if tree(d) != wanted:
                        errors.append("Retry left unexpected artifact")
                else:
                    if not run.returncode:
                        errors.append("Injected failure succeeded")
                    wanted = dict(before)
                    if operation == "unowned_temp":
                        name = "module.spv" if call == 1 else "reflection.json"
                        wanted[name + ".tmp.EVAL00"] = ["file", sha(b"UNOWNED")]
                    if operation == "publish_collision":
                        name = "module.spv" if call == 1 else "reflection.json"
                        wanted[name] = ["file", sha(b"UNOWNED")]
                    if tree(d) != wanted:
                        errors.append("Ordinary rollback/unowned cleanup invariant failed")
                self.record(label + "_fault_" + mode + "_" + operation + "_" + str(call),
                            errors, trace=trace, returncode=run.returncode, stderr=run.stderr)

    def build_wrappers(self) -> tuple[Path, Path]:
        build = self.args.evidence / "build"
        build.mkdir()
        owner = self.args.source_root
        sources = sorted((owner / "src").glob("*.c"))
        if not sources:
            raise FileNotFoundError("No candidate C sources at " + str(owner))
        wrapper = Path(__file__).with_name("cli_io_wrap_m5.c")
        hashes = {str(p): sha(p.read_bytes()) for p in sources + [wrapper]}
        paths = []
        for label, extra in (("wrapped", []), ("sanitized", ["-O1", "-g",
                             "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                             "-fno-pie", "-no-pie"])):
            output = build / ("cli_" + label)
            command = ["gcc", *CFLAGS, *extra, "-I" + str(owner / "include"),
                       *map(str, sources), str(wrapper),
                       "-Wl," + ",".join("--wrap=" + name for name in WRAPS),
                       "-o", str(output)]
            run = subprocess.run(command, capture_output=True, text=True,
                                 timeout=120, check=False)
            self.commands.append({"command": command, "returncode": run.returncode,
                                  "stdout": run.stdout, "stderr": run.stderr})
            self.record("build_" + label, [] if not run.returncode else [run.stderr],
                        source_hashes=hashes)
            if run.returncode:
                raise RuntimeError("CLI wrapper build failed; see first receipt")
            paths.append(output)
        return paths[0], paths[1]

    def finish(self) -> int:
        report = {"checks": len(self.records),
                  "passed": sum(r["passed"] for r in self.records),
                  "failed": [r for r in self.records if not r["passed"]],
                  "cli_sha256": sha(self.args.cli.read_bytes()),
                  "validator_sha256": sha(self.args.validator.read_bytes()),
                  "baseline_sha256": sha(self.args.baseline.read_bytes())
                      if self.args.baseline else None,
                  "records": self.records}
        for name, value in (("results.json", report), ("commands.json", self.commands)):
            (self.args.evidence / name).write_text(
                json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "records"},
                         indent=2, sort_keys=True), flush=True)
        return int(bool(report["failed"]))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "cli", "validator", "evidence"):
        parser.add_argument("--" + name, type=Path, required=True,
                            help="Explicit input or fresh evidence directory.")
    parser.add_argument("--baseline", type=Path,
                        help="Optional preserved M4 CLI for exact legacy bytes.")
    parser.add_argument("--source-root", type=Path,
                        help="Frozen candidate C owner; default repo/runtime/c/compiler.")
    parser.add_argument("--no-sanitizers", action="store_true",
                        help="Run ordinary prebuilt checks only; report reduced coverage.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    for key in ("repo", "cli", "validator", "evidence", "baseline", "source_root"):
        value = getattr(args, key)
        if value is not None:
            setattr(args, key, value.resolve())
    if args.source_root is None:
        args.source_root = args.repo / "runtime/c/compiler"
    args.evidence.mkdir(parents=True, exist_ok=False)
    checks = CheckCli(args)
    try:
        checks.positive(args.cli, "candidate")
        checks.legacy_baseline()
        checks.arguments(args.cli, "candidate")
        checks.paths(args.cli, "candidate")
        if not args.no_sanitizers:
            wrapped, sanitized = checks.build_wrappers()
            checks.faults(wrapped, "ordinary")
            checks.positive(sanitized, "sanitized")
            checks.arguments(sanitized, "sanitized")
            checks.paths(sanitized, "sanitized")
            checks.faults(sanitized, "sanitized")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        checks.record("stage_exception", [type(exc).__name__ + ": " + str(exc)])
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
