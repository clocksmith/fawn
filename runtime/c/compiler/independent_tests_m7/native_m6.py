"""Independent M6 native, static-stack and retained CLI acceptance stage."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

import cli_m5
import native_stage_m5 as retained

HERE = Path(__file__).resolve().parent
DISK_CAP = 256 * 1024 * 1024
LOG_CAP = 1024 * 1024
WRAPPER_NAMES = ("malloc", "calloc", "realloc", "free", "aligned_alloc",
                 "posix_memalign")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def disk_usage(root: Path) -> int:
    return sum(path.stat().st_size for path in root.rglob("*")
               if path.is_file() and not path.is_symlink())


def installed(name: str | Path) -> Path:
    selected = shutil.which(str(name))
    if selected is None:
        raise FileNotFoundError(f"Installed tool required: {name}; no install fallback")
    return Path(selected).resolve()


class Stage(retained.Stage):
    def run(self, name: str, arguments: list[str | Path]) -> str:
        command = [str(value) for value in arguments]
        started = time.monotonic()
        run = subprocess.run(command, cwd=HERE, env=self.environment,
                             capture_output=True, text=True, timeout=180,
                             check=False)
        output = run.stdout + run.stderr
        if len(output.encode("utf-8")) > LOG_CAP:
            raise RuntimeError(f"{name} exceeded the 1 MiB command-log cap")
        (self.output / (name + ".log")).write_text(output, encoding="utf-8")
        row = {"name": name, "command": command, "returncode": run.returncode,
               "elapsed_seconds": time.monotonic() - started}
        self.commands.append(row)
        write_json(self.output / "commands.json", self.commands)
        print(name + (": PASS" if run.returncode == 0 else ": FAIL"), flush=True)
        if run.returncode:
            failure = self.output / "FIRST_FAILURE.json"
            if not failure.exists():
                write_json(failure, row)
            raise RuntimeError(f"{name} exited {run.returncode}; see {name}.log")
        if disk_usage(self.output) > DISK_CAP:
            raise RuntimeError("Native output exceeded the frozen 256 MiB disk cap")
        return output


class CliChecks(cli_m5.CheckCli):
    def record(self, case: str, errors: list[str], **details: object) -> None:
        super().record(case, errors, **details)
        if not self.records[-1]["passed"]:
            failure = self.args.evidence / "FIRST_FAILURE.json"
            if not failure.exists():
                write_json(failure, self.records[-1])
        if self.counter % 20 == 0 and disk_usage(self.args.evidence) > DISK_CAP:
            raise RuntimeError("CLI output exceeded the frozen 256 MiB disk cap")

    def legacy_evidence(self) -> None:
        sources = [("synthetic", cli_m5.STORAGE)]
        ledger = json.loads((self.args.repo / "docs/c-migration/corpus-coverage-m4.json")
                            .read_text(encoding="utf-8"))
        sources += [(Path(row["source"]).stem,
                     (self.args.repo / row["source"]).read_bytes())
                    for row in ledger["records"] if row["admitted"]]
        for name, source in sources:
            expected = cli_m5.LEGACY_GOLDEN[cli_m5.sha(source)]
            binaries = [("candidate", self.args.cli)]
            if self.args.baseline is not None:
                binaries.insert(0, ("baseline", self.args.baseline))
            identities = {}
            for label, binary in binaries:
                directory = self.directory("legacy_" + label + "_" + name)
                (directory / "source.wgsl").write_bytes(source)
                run, _ = self.invoke(binary, self.parts(directory, "legacy"), directory)
                errors = []
                if run.returncode:
                    errors.append(label + " rejected retained source: " + run.stderr)
                else:
                    identities[label] = sha256(directory / "module.spv")
                    if (directory / "reflection.json").read_bytes() != expected["json"].encode():
                        errors.append(label + " changed retained legacy JSON")
                    errors += self.validate(directory / "module.spv")
                self.record("legacy_metadata_" + label + "_" + name, errors,
                            returncode=run.returncode, stderr=run.stderr)
            self.record("legacy_revision_evidence_" + name, [],
                        source_sha256=cli_m5.sha(source), output_hashes=identities,
                        old_golden_sha256=expected["spirv_sha256"],
                        matches_old_golden=identities.get("candidate") == expected["spirv_sha256"],
                        cross_revision_identity_required=False)

    def m6_positive(self, cli: Path, label: str) -> None:
        storage = "@group(0) @binding(0) var<storage,read_write> d:array<u32>;"
        cases = [
            ("empty_zero", "@compute @workgroup_size(1) fn main(){}", [1, 1, 1], []),
            ("dimensions", "@compute @workgroup_size(4,2,3,) fn main(){return;}", [4, 2, 3], []),
            ("five_inputs", storage + "@compute @workgroup_size(4,2,2) fn main("
             "@builtin(local_invocation_index) li:u32,@builtin(num_workgroups) n:vec3u,"
             "@builtin(workgroup_id) w:vec3u,@builtin(global_invocation_id) g:vec3u,"
             "@builtin(local_invocation_id) l:vec3u,){d[g.x]=li+n.y+w.z+l.y;}",
             [4, 2, 2], [cli_m5.binding("d", 0, 0, "u32", "read_write")]),
            ("indirect", storage + "@compute @workgroup_size(2,2) fn main("
             "@builtin(global_invocation_id) p:vec3u){let i=d[d[p.x]];"
             "if(d[i+1u]<d[d[p.y]]){d[d[i]*3u+1u]=d[d[p.z]];}}",
             [2, 2, 1], [cli_m5.binding("d", 0, 0, "u32", "read_write")]),
        ]
        for name, source, dimensions, bindings in cases:
            expected = cli_m5.reflection(bindings, [])
            expected["workgroup_size"] = dimensions
            previous = None
            for mode in cli_m5.MODES:
                directory = self.directory(label + "_m6_" + name + "_" + mode)
                (directory / "source.wgsl").write_text(source, encoding="utf-8")
                run, trace = self.invoke(cli, self.parts(directory, mode), directory)
                errors = []
                if run.returncode:
                    errors.append("M6 admitted CLI source rejected: " + run.stderr)
                else:
                    errors += self.validate(directory / "module.spv")
                    raw = (directory / "module.spv").read_bytes()
                    if previous is not None and previous != raw:
                        errors.append("Current binary/ABI1/v2 words differ")
                    previous = raw
                    if mode == "v2":
                        actual = json.loads((directory / "reflection.json").read_text(encoding="utf-8"))
                        if actual != expected:
                            errors.append("M6 exact v2 schema/value mismatch")
                    elif mode == "legacy":
                        actual = json.loads((directory / "reflection.json").read_text(encoding="utf-8"))
                        if actual["entry_point"] != "main" or actual["workgroup_size"] != dimensions:
                            errors.append("M6 legacy entry/workgroup mismatch")
                        if actual["binding_count"] != len(bindings):
                            errors.append("M6 legacy binding count mismatch")
                    names = {"source.wgsl", "module.spv"}
                    if mode != "binary":
                        names.add("reflection.json")
                    if {path.name for path in directory.iterdir()} != names:
                        errors.append("M6 CLI left unexpected output or temporary")
                self.record(label + "_m6_" + name + "_" + mode, errors,
                            returncode=run.returncode, stderr=run.stderr, trace=trace)


def measure_stack(stage: Stage, cc: Path, sources: list[Path],
                  cli_sources: list[Path]) -> dict[str, object]:
    frames = []
    cli_names = {path.stem for path in cli_sources}
    for source in sources + cli_sources:
        stage.run("stack_" + source.stem, [cc, *retained.C_FLAGS,
                  "-fstack-usage", "-fcallgraph-info=su", "-c", source,
                  "-o", stage.build / (source.stem + ".o")])
    for usage in sorted(stage.build.glob("*.su")):
        for line in usage.read_text(encoding="utf-8").splitlines():
            location, size, qualifier = line.split("\t")
            function = location.split(":")[-1]
            limit = None if usage.stem in cli_names else (
                96272 if function == "doe_compiler_compile" else 8192)
            frames.append({"function": function, "source_location": location,
                           "bytes": int(size), "qualifier": qualifier, "limit": limit,
                           "scope": "CLI_report_only" if limit is None else "library_budget"})
    write_json(stage.output / "stack-frames.json", frames)
    if not any(row["function"] == "doe_compiler_compile" for row in frames):
        raise AssertionError("Missing ABI1 wrapper static frame")
    bad = [row for row in frames if row["limit"] is not None and
           (row["bytes"] > row["limit"] or "unbounded" in row["qualifier"])]
    if bad:
        raise AssertionError(f"Frozen M6 static-stack budget failure: {bad}")
    report = {"expression_depth": 18, "if_depth": 4,
              "node_capacity": 320, "ir_instructions": 385, "ir_values": 386,
              "frame_evidence": "stack-frames.json and build/*.ci",
              "recursion_review": "Source-bound recursion review is reported separately after inspecting the frozen candidate.",
              "scope": "Individual GCC -O2 library frames; CLI callers report-only. No combined high-water or external-libc-frame claim."}
    write_json(stage.output / "recursive-callee-bounds.json", report)
    return {"abi1_frame": next(row["bytes"] for row in frames
                              if row["function"] == "doe_compiler_compile"),
            "largest_other_library_frame": max(row["bytes"] for row in frames
                if row["limit"] == 8192)}


def native_stage(args: argparse.Namespace, stage: Stage, compiler: Path,
                 library: Path, cc: Path, cxx: Path, validator: Path) -> dict[str, object]:
    sources = sorted(path for path in (compiler / "src").glob("*.c")
                     if not path.name.startswith("cli"))
    cli_sources = sorted((compiler / "src").glob("cli*.c"))
    include = "-I" + str(compiler / "include")
    witness = HERE / "native/inputs/package_vector_scale_add.wgsl"
    corpus = sorted(args.corpus.resolve().glob("*.wgsl")) if args.corpus else []
    if len(corpus) > 512:
        raise ValueError("Native corpus exceeds 512-file cap")
    bound = [*sources, *cli_sources, *compiler.glob("src/*.h"),
             *compiler.glob("include/*.h"), compiler / "src/exports.map", library,
             Path(__file__), HERE / "native_m6.c", HERE / "native_m5.c",
             HERE / "native_layout_m5.cpp", witness,
             *sorted((HERE / "native/retained").glob("*.c")), *corpus]
    write_json(stage.output / "source-artifact-hashes.json",
               {str(path): sha256(path) for path in bound})
    for name, expected in (("doe_compiler.h", retained.HEADER_SHA),
                           ("doe_compiler_v2.h", retained.V2_HEADER_SHA)):
        if sha256(compiler / "include" / name) != expected:
            raise AssertionError(f"Public header is not byte-identical: {name}")
    for name, expected in retained.RETAINED_HASHES.items():
        if sha256(HERE / "native/retained" / name) != expected:
            raise AssertionError(f"Retained source changed: {name}")
    if sha256(witness) != retained.WITNESS_SHA:
        raise AssertionError("Unchanged package source hash mismatch")
    for label, tool in (("gcc", cc), ("gxx", cxx), ("python", Path(sys.executable)),
                        ("validator", validator)):
        stage.run(label + "_version", [tool, "--version"])
    exports = stage.run("exports", [installed("nm"), "-D", "--defined-only", library])
    if {line.split()[-1] for line in exports.splitlines() if line.split()} != retained.EXPECTED_EXPORTS:
        raise AssertionError("Native exported symbol allowlist mismatch")
    undefined = stage.run("undefined_symbols", [installed("nm"), "-D", "--undefined-only", library])
    if re.search(r"\b(?:malloc|calloc|realloc|free|aligned_alloc|posix_memalign|mmap)(?:@|\s|$)", undefined):
        raise AssertionError("Library imports forbidden allocator")
    stage.run("dependencies", [installed("ldd"), library])
    wrapper_flags = ["-Wl,--wrap=" + name for name in WRAPPER_NAMES]
    workspace_size = None
    for label, extra, linked in (
            ("prebuilt", [], [library, "-Wl,-rpath," + str(library.parent)]),
            ("source", ["-DWRAP_ALLOCATORS"], [*sources, *wrapper_flags]),
            ("sanitize", [*retained.SAN_FLAGS, "-DWRAP_ALLOCATORS"], [*sources, *wrapper_flags])):
        binary = stage.build / ("native_" + label)
        stage.run(label + "_build", [cc, *retained.C_FLAGS, *extra, include,
                  HERE / "native_m6.c", *linked, "-pthread", "-o", binary])
        output = stage.run(label + "_native", [binary, witness,
                           stage.output / (label + "-witness.spv"), *corpus])
        match = re.search(r"workspace_size=(\d+) workspace_alignment=(\d+)", output)
        if not match or int(match.group(1)) > 96208:
            raise AssertionError("Missing/oversized queried workspace receipt")
        workspace_size = int(match.group(1))
        stage.run(label + "_validate", [validator, "--target-env", "vulkan1.1",
                  stage.output / (label + "-witness.spv")])
    words = [(stage.output / (label + "-witness.spv")).read_bytes()
             for label in ("prebuilt", "source", "sanitize")]
    if words[0] != words[1] or words[0] != words[2]:
        raise AssertionError("Current source/prebuilt/sanitized SPIR-V differs")
    cpp = stage.build / "native_cpp"
    stage.run("cpp_layout_build", [cxx, *retained.CPP_FLAGS, include,
              HERE / "native_layout_m5.cpp", library,
              "-Wl,-rpath," + str(library.parent), "-o", cpp])
    stage.run("cpp_layout", [cpp])
    for label, tool, flags in (("gcc", cc, retained.C_FLAGS), ("gxx", cxx, retained.CPP_FLAGS)):
        binary = stage.build / (label + "_retained")
        language = ["-x", "c++"] if label == "gxx" else []
        reset = ["-x", "none"] if label == "gxx" else []
        stage.run(label + "_retained_build", [tool, *flags, include, *language,
                  HERE / "native/retained/consumer_m4.c", *reset, library,
                  "-Wl,-rpath," + str(library.parent), "-o", binary])
        stage.run(label + "_retained", [binary])
    binary = stage.build / "retained_sanitize"
    stage.run("retained_sanitize_build", [cc, *retained.C_FLAGS, *retained.SAN_FLAGS,
              include, HERE / "native/retained/sanitizer_m4.c", *sources, "-o", binary])
    stage.run("retained_sanitize", [binary])
    frames = measure_stack(stage, cc, sources, cli_sources)
    return {"workspace_bytes": workspace_size, **frames, "native_corpus_files": len(corpus),
            "m5_mutations_per_run": 12000, "m6_mutations_per_run": 5120,
            "retained_m4_sanitizer_mutations": 50000, "physical_execution": False}


def cli_stage(args: argparse.Namespace, stage: Stage, compiler: Path,
              cli: Path, cc: Path, validator: Path) -> dict[str, object]:
    directory = stage.output / "cli"
    directory.mkdir()
    cli_args = argparse.Namespace(repo=args.repo, cli=cli, validator=validator,
                                  evidence=directory, baseline=args.baseline_cli,
                                  source_root=compiler, no_sanitizers=False)
    checks = CliChecks(cli_args)
    checks.positive(cli, "candidate")
    checks.m6_positive(cli, "candidate")
    checks.legacy_evidence()
    checks.arguments(cli, "candidate")
    checks.paths(cli, "candidate")
    sources = sorted((compiler / "src").glob("*.c"))
    for label, extra in (("ordinary", []), ("sanitized", retained.SAN_FLAGS)):
        binary = stage.build / ("cli_" + label)
        stage.run("cli_" + label + "_build", [cc, *retained.C_FLAGS, *extra,
                  "-I" + str(compiler / "include"), *sources, HERE / "cli_io_wrap_m5.c",
                  "-Wl," + ",".join("--wrap=" + name for name in cli_m5.WRAPS), "-o", binary])
        checks.faults(binary, label)
        if label == "sanitized":
            checks.positive(binary, label)
            checks.m6_positive(binary, label)
            checks.arguments(binary, label)
            checks.paths(binary, label)
    status = checks.finish()
    if status:
        raise AssertionError("Retained/M6 CLI acceptance failed; see cli/FIRST_FAILURE.json")
    return {"checks": len(checks.records), "passed": len(checks.records),
            "cross_revision_bytes": "report_only", "current_mode_bytes": "required_identical"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "build", "out"):
        parser.add_argument("--" + name, required=True, type=Path,
                            help="Explicit repository, central build, or fresh output directory.")
    parser.add_argument("--source-root", type=Path,
                        help="Frozen compiler owner; defaults to repo/runtime/c/compiler.")
    parser.add_argument("--library", type=Path, help="Exact prebuilt library; otherwise find one libdoe_compiler.so in build.")
    parser.add_argument("--cli", type=Path, help="Exact prebuilt CLI; otherwise find one doe-compiler-c in build.")
    parser.add_argument("--corpus", type=Path, help="Optional bounded WGSL corpus for native guard/sanitizer admission.")
    parser.add_argument("--baseline-cli", type=Path, help="Optional retained CLI for report-only cross-revision words.")
    parser.add_argument("--cc", default="gcc", help="Installed GCC executable.")
    parser.add_argument("--cxx", default="g++", help="Installed G++ executable.")
    parser.add_argument("--validator", default="spirv-val", help="Installed spirv-val executable.")
    parser.add_argument("--stage", choices=("all", "native", "cli"), default="all",
                        help="Run both stages or one explicitly reported subset.")
    return parser.parse_args()


def find_artifact(root: Path, pattern: str, explicit: Path | None) -> Path:
    if explicit is not None:
        if not explicit.is_file():
            raise FileNotFoundError(explicit)
        return explicit.resolve()
    matches = sorted(root.rglob(pattern))
    if len(matches) != 1:
        raise ValueError(f"Expected one {pattern} under {root}; pass its exact path")
    return matches[0].resolve()


def main() -> int:
    args = parse_args()
    args.repo = args.repo.resolve()
    compiler = args.source_root.resolve() if args.source_root else args.repo / "runtime/c/compiler"
    stage = Stage(args.out.resolve())
    result = {"status": "FAIL", "stage": args.stage, "source_root": str(compiler),
              "disk_cap_bytes": DISK_CAP, "physical_execution": False}
    try:
        cc, cxx, validator = installed(args.cc), installed(args.cxx), installed(args.validator)
        library = find_artifact(args.build, "libdoe_compiler.so", args.library)
        cli = find_artifact(args.build, "doe-compiler-c", args.cli)
        result["tools"] = {"cc": str(cc), "cxx": str(cxx), "validator": str(validator),
                           "python": sys.executable}
        result["artifacts"] = {str(path): sha256(path) for path in (library, cli)}
        if args.stage in ("all", "native"):
            result["native"] = native_stage(args, stage, compiler, library, cc, cxx, validator)
        if args.stage in ("all", "cli"):
            result["cli"] = cli_stage(args, stage, compiler, cli, cc, validator)
        result["status"] = "PASS"
    except (OSError, RuntimeError, ValueError, AssertionError, subprocess.TimeoutExpired) as error:
        result["error"] = type(error).__name__ + ": " + str(error)
        failure = stage.output / "FIRST_FAILURE.json"
        if not failure.exists():
            write_json(failure, result)
    result["output_bytes"] = disk_usage(stage.output)
    write_json(stage.output / "result.json", result)
    print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return int(result["status"] != "PASS")


if __name__ == "__main__":
    raise SystemExit(main())
