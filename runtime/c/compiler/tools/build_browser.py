#!/usr/bin/env python3
"""Build the import-free C browser compiler with the pinned WASI SDK.

No downloads or Zig invocation occur here. WASI_SDK_PATH names an extracted SDK;
the archive URL, digest and byte count are in config/toolchains.json.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

from source_inventory import compiler_sources

COMPILER = Path(__file__).resolve().parents[1]
ROOT = COMPILER.parents[2]
ADAPTERS = ["src/rewrite.c", "browser/job.c", "browser/wasm.c"]
EXPORTS = ["source_limit", "reserve_source", "compile", "output_length", "release_job"]


def source_files() -> list[Path]:
    return compiler_sources(COMPILER) + [COMPILER / path for path in ADAPTERS]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4")
    sdk_path = os.environ.get("WASI_SDK_PATH")
    if not sdk_path:
        parser.error("set WASI_SDK_PATH to the checksum-verified WASI SDK from config/toolchains.json")
    sdk = Path(sdk_path).resolve()
    clang = sdk / "bin/clang"
    version = subprocess.check_output([str(clang), "--version"], text=True, timeout=10)
    if "clang version 23.1.0-wasi-sdk" not in version:
        parser.error("browser compiler requires WASI SDK 34.0 (clang 23.1.0-wasi-sdk)")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = output.parent / "objects"
    objects.mkdir(exist_ok=True)
    flags = ["--target=wasm32-wasip1", "-std=c11", "-O2", "-fno-fast-math",
             "-ffp-contract=off", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
             "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wswitch-enum",
             "-Werror", "-ffunction-sections", "-fdata-sections"]
    sources = source_files()

    def compile_source(source: Path) -> Path:
        obj = objects / (str(source.relative_to(COMPILER)).replace("/", "_") + ".o")
        subprocess.run([str(clang), *flags, "-c", str(source), "-o", str(obj)],
                       check=True, timeout=90)
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_source, sources))
    # Core C globals are audited by the native owner build. This instance-only
    # adapter deliberately owns four job slots; catch any unreviewed additions.
    symbols = subprocess.check_output([str(sdk / "bin/llvm-nm"), "--defined-only",
        "--format=posix", str(compiled[-1])], text=True, timeout=10)
    mutable = {fields[0] for line in symbols.splitlines()
               if len(fields := line.split()) >= 2 and fields[1] in {"d", "D", "b", "B"}}
    if mutable != {"input", "output", "input_size", "output_size"}:
        raise RuntimeError(f"unexpected browser instance state: {sorted(mutable)}")
    temporary = output.with_suffix(".wasm.tmp")
    subprocess.run([str(clang), "--target=wasm32-wasip1", "-mexec-model=reactor",
                    *map(str, compiled), "-lm", "-Wl,--export-memory",
                    "-Wl,--max-memory=67108864", "-Wl,-z,stack-size=1048576",
                    "-Wl,--strip-all", *[f"-Wl,--export={name}" for name in EXPORTS],
                    "-o", str(temporary)], check=True, timeout=90)
    # The shipped worker instantiates with {}. A WASI syscall import is a build
    # failure, even if a developer could supply a host implementation.
    subprocess.run(["node", "--input-type=module", "-e",
                    "import {readFileSync} from 'node:fs';"
                    "const m=new WebAssembly.Module(readFileSync(process.argv[1]));"
                    "const i=WebAssembly.Module.imports(m);"
                    "if(i.length)throw Error(JSON.stringify(i));", str(temporary)],
                   check=True, timeout=20)
    temporary.replace(output)
    inputs = set(sources)
    inputs.update(COMPILER.glob("include/*.h"))
    inputs.update(COMPILER.glob("src/*.h"))
    inputs.update(COMPILER.glob("browser/*.h"))
    inputs.add(COMPILER / "browser/THIRD_PARTY_NOTICES.txt")
    inputs.update(COMPILER.glob("targets/*.h"))
    inputs.update(COMPILER.glob("targets/*/*.h"))
    inputs.update(COMPILER.glob("targets/*/sources.mk"))
    inputs.update([Path(__file__).resolve(), COMPILER / "tools/source_inventory.py", COMPILER / "src/sources.txt", ROOT / "config/toolchains.json"])
    source_hash = hashlib.sha256()
    for path in sorted(inputs):
        source_hash.update(str(path.relative_to(ROOT)).encode() + b"\0")
        source_hash.update(path.read_bytes() + b"\0")
    data = output.read_bytes()
    manifest = {
        "schemaVersion": 1, "provider": "c", "abiVersion": 1,
        "toolchain": version.splitlines()[0],
        "toolchainPin": json.loads((ROOT / "config/toolchains.json").read_text())["toolchains"]["wasiSdk"],
        "sourceHash": source_hash.hexdigest(), "sha256": hashlib.sha256(data).hexdigest(),
        "byteLength": len(data), "maximumMemoryBytes": 67108864,
        "imports": [], "sourceFiles": [str(p.relative_to(ROOT)) for p in sorted(inputs)],
    }
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({key: value for key, value in manifest.items() if key != "sourceFiles"}))


if __name__ == "__main__":
    main()
