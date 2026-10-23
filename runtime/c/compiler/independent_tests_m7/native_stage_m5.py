"""Independent M5 native ABI, workspace, stack and sanitizer evaluation."""
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

HERE = Path(__file__).resolve().parent
C_FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
           "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
           "-Wmissing-prototypes", "-Wswitch-enum", "-Werror"]
CPP_FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic",
             "-Wconversion", "-Wshadow", "-Werror", "-fno-rtti",
             "-fno-exceptions"]
SAN_FLAGS = ["-O1", "-g", "-fno-omit-frame-pointer",
             "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
LIB_NAMES = ("numeric", "lex", "parse", "validate", "reflection", "spirv",
             "compile")
EXPECTED_EXPORTS = {"doe_compiler_compile", "doe_compiler_compile_v2",
                    "doe_compiler_workspace_requirements"}
HEADER_SHA = "5bf4f5e5a4e4624c094320ad9ec97119278bc7f6f06b0e47c01e444d42c6dc5b"
V2_HEADER_SHA = "b0a60c2f05b70dba0786365dadd81b993accbd93f7f68f0e3accf86a815ae891"
WITNESS_SHA = "8f2b3e2bb6955c1f7507936773589195c80e6a119cbd50d975f652d4f834e8c1"
RETAINED_HASHES = {
    "consumer_m4.c": "3e06d6471184593434d26b1dfdacf61ba221f82c8becc389262ae6ccc23e1bf4",
    "sanitizer_m4.c": "020e5fd7e7078253b8a4a81a9deef4f978fb9e8481bbce26f93d622b16dfaaed",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path,
                        help="Immutable compiler snapshot or repository root.")
    parser.add_argument("--library", required=True, type=Path,
                        help="Already-built candidate shared library.")
    parser.add_argument("--evidence", required=True, type=Path,
                        help="Fresh output directory for native receipts/builds.")
    parser.add_argument("--validator", type=Path,
                        help="Existing approved spirv-val binary, if available.")
    parser.add_argument("--corpus", type=Path,
                        help="Independent WGSL corpus directory to sanitize.")
    return parser.parse_args()


class Stage:
    def __init__(self, output: Path) -> None:
        self.output = output
        self.commands: list[dict[str, object]] = []
        self.output.mkdir(parents=True, exist_ok=False)
        self.build = self.output / "build"
        self.build.mkdir()
        (self.build / "tmp").mkdir()
        self.environment = dict(os.environ, TMPDIR=str(self.build / "tmp"),
                                PYTHONDONTWRITEBYTECODE="1",
                                ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                                UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

    def run(self, name: str, arguments: list[str | Path]) -> str:
        command = [str(value) for value in arguments]
        started = time.monotonic()
        result = subprocess.run(command, cwd=HERE, env=self.environment,
                                capture_output=True, text=True, check=False)
        elapsed = time.monotonic() - started
        output = result.stdout + result.stderr
        (self.output / (name + ".log")).write_text(output, encoding="utf-8")
        self.commands.append({"name": name, "command": command,
                              "returncode": result.returncode,
                              "elapsed_seconds": elapsed})
        write_json(self.output / "commands.json", self.commands)
        print(name + (": PASS" if result.returncode == 0 else ": FAIL"), flush=True)
        if result.returncode != 0:
            write_json(self.output / "FIRST_FAILURE.json", self.commands[-1])
            print(output, flush=True)
            raise RuntimeError(f"{name} exited {result.returncode}; first failure retained")
        return output


def stack_report(stage: Stage, sources: list[Path], cli_sources: list[Path]) -> None:
    for source in sources + cli_sources:
        stage.run("stack_" + source.stem,
                  ["gcc", *C_FLAGS, "-fstack-usage", "-fcallgraph-info=su",
                   "-c", source, "-o", stage.build / (source.stem + ".o")])
    frames: list[dict[str, object]] = []
    for usage in sorted(stage.build.glob("*.su")):
        for line in usage.read_text(encoding="utf-8").splitlines():
            position, size_text, qualifier = line.split("\t")
            name = position.split(":")[-1]
            size = int(size_text)
            is_library = usage.stem not in {source.stem for source in cli_sources}
            limit = (105328 if name == "doe_compiler_compile" else 8192) if is_library else None
            frames.append({"function": name, "source_location": position,
                           "bytes": size, "qualifier": qualifier, "limit": limit,
                           "scope": "library_budget" if is_library else "CLI_report_only"})
    write_json(stage.output / "stack-frames.json", frames)
    bad = [row for row in frames if row["limit"] is not None and
           (int(row["bytes"]) > int(row["limit"]) or
            "unbounded" in str(row["qualifier"]))]
    recursion = {
        "expression_parse": {
            "functions": ["expression", "primary"],
            "maximum_active_frames_each": 17,
            "bound": "primary rejects nesting ==16; expression increments nesting on every recursive edge; precedence loop does not recurse without this increment",
        },
        "block_parse": {"functions": ["block"], "maximum_active_frames_each": 5,
                        "bound": "Root depth0 plus at most four admitted nested if blocks"},
        "expression_lower": {"functions": ["lower_expression"],
                             "maximum_active_frames_each": 16,
                             "bound": "Validated expression-node depth is at most16; child descent strictly reduces the finite AST"},
        "block_lower": {"functions": ["lower_block"], "maximum_active_frames_each": 5,
                        "bound": "Same validated four-if-depth source tree; sibling arms run sequentially"},
        "block_emit": {"functions": ["emit_block", "emit_selection"],
                       "maximum_active_frames_each": 5,
                       "bound": "Root block and four structured levels; emitter additionally checks depth before nested selection"},
    }
    for entry in recursion.values():
        entry["measured_frames"] = [row for row in frames if
            any(str(row["function"]).split(".")[0] == function
                for function in entry["functions"])]
        entry["recursive_frames_only_conservative_bytes"] = sum(
            int(row["bytes"]) * int(entry["maximum_active_frames_each"])
            for row in entry["measured_frames"])
    write_json(stage.output / "recursive-callee-bounds.json", {
        "recursion": recursion,
        "callee_evidence": "All static frames are in stack-frames.json. GCC -O2 callgraph-info=su graphs (*.ci) include direct callee edges, including inlining effects in emitted frames.",
        "scope": "Frozen8192 budget applies to caller-workspace library entrypoint/core and all library callees; ABI1 wrapper limit105328. CLI source/word/binary/path buffers are measured separately without an8192 claim.",
        "limitation": "Per-frame/recursion accounting only, not a measured combined stack high-water mark; libc external frames are not measured.",
        "capacities": {"source_bytes": 65536, "tokens": 2048, "name_bytes": 63,
                       "words": 4096, "statements": 16, "expressions": 64,
                       "if_depth": 4, "expression_depth": 16,
                       "ir_instructions": 385, "ir_values": 386},
    })
    if bad:
        raise AssertionError(f"Frozen stack budget failure: {bad}")
    if not any(row["function"] == "doe_compiler_compile" for row in frames):
        raise AssertionError("Missing ABI1 wrapper stack measurement")


def main() -> int:
    arguments = parse_args()
    root = arguments.source_root.resolve()
    compiler = root if (root / "src" / "compile.c").exists() else root / "runtime/c/compiler"
    library = arguments.library.resolve()
    stage = Stage(arguments.evidence.resolve())
    include = "-I" + str(compiler / "include")
    sources = [compiler / "src" / (name + ".c") for name in LIB_NAMES]
    cli_sources = [compiler / "src/cli.c", compiler / "src/cli_json.c"]
    witness = HERE / "native/inputs/package_vector_scale_add.wgsl"
    retained = HERE / "native/retained"
    corpus = sorted(arguments.corpus.resolve().glob("*.wgsl")) if arguments.corpus else []
    bound_files = [*sources, *cli_sources, *compiler.glob("src/*.h"),
                   *compiler.glob("include/*.h"), compiler / "src/exports.map",
                   compiler / "Makefile", library, Path(__file__).resolve(),
                   HERE / "native_m5.c", HERE / "native_layout_m5.cpp", witness,
                   *retained.glob("*.c"), *corpus]
    write_json(stage.output / "source-artifact-hashes.json",
               {str(path): sha256(path) for path in bound_files})
    try:
        if sha256(compiler / "include/doe_compiler.h") != HEADER_SHA:
            raise AssertionError("ABI1 header is not byte-identical to frozen contract")
        if sha256(compiler / "include/doe_compiler_v2.h") != V2_HEADER_SHA:
            raise AssertionError("ABI2 header changed after contract freeze")
        if sha256(witness) != WITNESS_SHA:
            raise AssertionError("Unchanged witness hash mismatch")
        for name, expected in RETAINED_HASHES.items():
            if sha256(retained / name) != expected:
                raise AssertionError(f"Historical native source modified: {name}")
        for compiler_name in ("gcc", "g++"):
            if shutil.which(compiler_name) is None:
                raise FileNotFoundError(f"Installed {compiler_name} required; no install fallback")
            stage.run(compiler_name + "_version", [compiler_name, "--version"])
        stage.run("python_version", [sys.executable, "--version"])
        exports = stage.run("exports", ["nm", "-D", "--defined-only", library])
        symbols = {line.split()[-1] for line in exports.splitlines() if line.split()}
        if symbols != EXPECTED_EXPORTS:
            raise AssertionError(f"Export allowlist mismatch: {symbols}")
        undefined = stage.run("undefined_symbols", ["nm", "-D", "--undefined-only", library])
        if re.search(r"\b(?:malloc|calloc|realloc|free|aligned_alloc|posix_memalign|mmap)(?:@|\s|$)", undefined):
            raise AssertionError("Library has a forbidden allocator dependency")
        stage.run("dependencies", ["ldd", library])
        prebuilt = stage.build / "native_prebuilt"
        stage.run("native_prebuilt_build", ["gcc", *C_FLAGS, include,
                  HERE / "native_m5.c", library, "-Wl,-rpath," + str(library.parent),
                  "-pthread", "-o", prebuilt])
        stage.run("native_prebuilt", [prebuilt, witness, stage.output / "prebuilt-witness.spv", *corpus])
        cpp = stage.build / "native_cpp"
        stage.run("cpp_layout_build", ["g++", *CPP_FLAGS, include,
                  HERE / "native_layout_m5.cpp", library,
                  "-Wl,-rpath," + str(library.parent), "-o", cpp])
        stage.run("cpp_layout", [cpp])
        wrapper_flags = ["-Wl,--wrap=" + name for name in
                         ("malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign")]
        source_binary = stage.build / "native_source_wrapped"
        stage.run("native_source_build", ["gcc", *C_FLAGS, include, "-DWRAP_ALLOCATORS",
                  HERE / "native_m5.c", *sources, *wrapper_flags, "-pthread", "-o", source_binary])
        stage.run("native_source", [source_binary, witness, stage.output / "source-witness.spv", *corpus])
        if (stage.output / "prebuilt-witness.spv").read_bytes() != (stage.output / "source-witness.spv").read_bytes():
            raise AssertionError("Source/prebuilt witness outputs differ")
        for cc, flags in (("gcc", C_FLAGS), ("g++", CPP_FLAGS)):
            binary = stage.build / (cc + "_retained_m4")
            stage.run(cc + "_retained_build", [cc, *flags, include,
                      *(["-x", "c++"] if cc == "g++" else []), retained / "consumer_m4.c",
                      *(["-x", "none"] if cc == "g++" else []), library,
                      "-Wl,-rpath," + str(library.parent), "-o", binary])
            stage.run(cc + "_retained", [binary])
        sanitizer = stage.build / "native_sanitize"
        stage.run("native_sanitize_build", ["gcc", *C_FLAGS, *SAN_FLAGS, include,
                  "-DWRAP_ALLOCATORS", HERE / "native_m5.c", *sources,
                  *wrapper_flags, "-pthread", "-o", sanitizer])
        stage.run("native_sanitize", [sanitizer, witness, stage.output / "sanitizer-witness.spv", *corpus])
        retained_san = stage.build / "retained_sanitize"
        stage.run("retained_sanitize_build", ["gcc", *C_FLAGS, *SAN_FLAGS, include,
                  retained / "sanitizer_m4.c", *sources, "-o", retained_san])
        stage.run("retained_sanitize", [retained_san])
        if arguments.validator:
            validator = arguments.validator.resolve()
            stage.run("validator_version", [validator, "--version"])
            stage.run("witness_validation", [validator, "--target-env", "vulkan1.1",
                      stage.output / "prebuilt-witness.spv"])
        stack_report(stage, sources, cli_sources)
        outputs = [path for path in stage.build.iterdir() if path.is_file()]
        write_json(stage.output / "build-artifacts.json", {
            str(path): {"sha256": sha256(path), "bytes": path.stat().st_size}
            for path in outputs})
        write_json(stage.output / "result.json", {
            "status": "PASS", "source_root": str(compiler), "library": str(library),
            "corpus_cases": len(corpus), "m5_fuzz_per_native_run": 12000,
            "retained_m4_fuzz": 50000, "thread_count": 4,
            "sanitizers": ["AddressSanitizer", "UndefinedBehaviorSanitizer"],
            "leak_sanitizer": "disabled; allocator wrappers separately enforce no library allocation/free",
            "source_prebuilt_identical_witness": True,
            "validator_run": bool(arguments.validator),
            "stack_budget_scope": "library functions only; CLI frames reported separately",
            "physical_execution": False,
        })
        print("PASS native M5 stage", flush=True)
        return 0
    except (AssertionError, FileNotFoundError, RuntimeError, ValueError) as error:
        if not (stage.output / "FIRST_FAILURE.json").exists():
            write_json(stage.output / "FIRST_FAILURE.json", {"error": str(error)})
        write_json(stage.output / "result.json", {"status": "FAIL", "error": str(error)})
        print(f"FAIL native M5: {error}", flush=True)
        return 1
    finally:
        outputs = [path for path in stage.build.iterdir() if path.is_file()]
        write_json(stage.output / "build-artifacts.json", {
            str(path): {"sha256": sha256(path), "bytes": path.stat().st_size}
            for path in outputs})


if __name__ == "__main__":
    raise SystemExit(main())
