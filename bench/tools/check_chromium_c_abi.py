"""Compare frozen public C layouts and selector signatures with a pinned Dawn header.

The host compiler measures sizeof/alignment/member offsets and member sizes in
separate executables. Clang independently reports declared function signatures.
This CPU gate does not load a provider or establish GPU semantics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any

from runtime.c.native.tests.browser_required_procedures import required_names


ROOT = Path(__file__).resolve().parents[2]
RENAMED_CONSTANTS = {
    "DOE_SHARED_STYPE_IOSURFACE": "WGPUSType_SharedTextureMemoryIOSurfaceDescriptor",
    "DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR": "WGPUSType_SharedFenceMTLSharedEventDescriptor",
    "DOE_SHARED_STYPE_MTL_EVENT_EXPORT": "WGPUSType_SharedFenceMTLSharedEventExportInfo",
    "DOE_SHARED_FENCE_MTL_EVENT": "WGPUSharedFenceType_MTLSharedEvent",
}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def without_comments(text: str) -> str:
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def structures(text: str) -> dict[str, list[str]]:
    """Read field names only; the compiler remains the layout authority."""
    text = without_comments(text)
    result = {}
    for name, body in re.findall(r'typedef struct (WGPU\w+)\s*\{([^{}]+)\}', text, re.S):
        fields = []
        for declaration in body.split(';'):
            if not declaration.strip():
                continue
            for part in declaration.split(','):
                match = re.search(r'(\w+)\s*(?:\[[^]]*\])?\s*$', part)
                if not match:
                    raise ValueError(f"unrecognized field declaration in {name}: {part}")
                fields.append(match[1])
        if name in result and result[name] != fields:
            raise ValueError(f"conflicting struct definitions: {name}")
        result[name] = fields
    aliases = re.findall(r'typedef\s+(WGPU\w+)\s+(WGPU\w+)\s*;', text)
    missing = set(re.findall(r'typedef struct (WGPU\w+)\s*\{', text)) - result.keys()
    if missing:
        raise ValueError(f"unrecognized nested public struct definitions: {sorted(missing)}")
    for _ in range(len(aliases) + 1):
        for target, alias in aliases:
            if target in result:
                result[alias] = list(result[target])
    return result


def constants(text: str) -> dict[str, int]:
    return {name: int(value, 0) for name, value in re.findall(
        r'\b(WGPU\w+|DOE_SHARED\w+)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\b', without_comments(text))}


def compare_constants(c: dict[str, int], dawn: dict[str, int]) -> list[dict[str, Any]]:
    return [{"constant": name, "pinnedConstant": RENAMED_CONSTANTS.get(name, name),
             "c": value, "pinnedDawn": dawn.get(RENAMED_CONSTANTS.get(name, name))}
            for name, value in c.items() if dawn.get(RENAMED_CONSTANTS.get(name, name)) != value]


def compare_layouts(c: dict[str, dict[str, int]], dawn: dict[str, dict[str, int]]) -> list[dict[str, Any]]:
    failures = []
    for name, fields in c.items():
        if name not in dawn:
            failures.append({"struct": name, "reason": "C public struct absent from pinned Dawn header"})
        elif fields != dawn[name]:
            failures.append({"struct": name, "c": fields, "pinnedDawn": dawn[name]})
    return failures


def probe_source(headers: list[Path], layouts: dict[str, list[str]]) -> str:
    lines = ['#include <stdio.h>', '#include <stddef.h>']
    lines += ['#include ' + json.dumps(str(header)) for header in headers]
    lines.append('int main(void) {')
    for name, fields in sorted(layouts.items()):
        lines.append(f'printf("{name} sizeof %zu\\n", sizeof({name}));')
        lines.append(f'printf("{name} alignof %zu\\n", _Alignof({name}));')
        for field in fields:
            lines.append(f'printf("{name} {field}.offset %zu\\n", offsetof({name}, {field}));')
            lines.append(f'printf("{name} {field}.size %zu\\n", sizeof((({name} *)0)->{field}));')
    lines.append('return 0; }')
    return '\n'.join(lines) + '\n'


def signatures(tree: dict[str, Any]) -> dict[str, str]:
    result = {}
    def visit(node: dict[str, Any]) -> None:
        if node.get("kind") == "FunctionDecl" and node.get("name", "").startswith("wgpu"):
            result[node["name"]] = node.get("type", {}).get("qualType", "")
        for child in node.get("inner", []):
            visit(child)
    visit(tree)
    return result


def run(args: argparse.Namespace) -> dict[str, Any]:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compiler = Path(shutil.which(args.compiler) or args.compiler).resolve(strict=True)
    originals = [args.base_header.resolve(strict=True), args.header.resolve(strict=True),
                 *sorted(args.c_include.resolve(strict=True).glob('*.h')), args.selector.resolve(strict=True)]
    inputs = {str(path): sha256(path) for path in originals}
    implementation = Path(__file__).resolve()
    inputs[str(implementation)] = sha256(implementation)
    inputs[str(compiler)] = sha256(compiler)
    (out / 'checker.py').write_bytes(implementation.read_bytes())
    frozen = out / 'inputs'
    frozen.mkdir()
    c_dir = frozen / 'c'
    c_dir.mkdir()
    copied = {}
    for path in originals:
        if path == args.header.resolve():
            destination = frozen / 'pinned-dawn-webgpu.h'
        elif path == args.selector.resolve():
            destination = frozen / 'selector.patch'
        else:
            destination = c_dir / path.name
        destination.write_bytes(path.read_bytes())
        if sha256(destination) != inputs[str(path)]:
            raise ValueError(f"input changed while freezing: {path}")
        copied[path] = destination
    c_headers = [copied[path] for path in originals if path.parent == args.c_include.resolve()]
    c_headers.insert(0, copied[args.base_header.resolve()])
    c_text = '\n'.join(path.read_text() for path in c_headers)
    dawn_header = copied[args.header.resolve()]
    dawn_text = dawn_header.read_text()
    c_structs, dawn_structs = structures(c_text), structures(dawn_text)
    if not c_structs or not dawn_structs:
        raise ValueError("public headers contain no recognized structs")
    observed = {}
    declarations = {}
    commands = []

    def command(argv: list[str], name: str) -> subprocess.CompletedProcess:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=args.timeout)
        (out / (name + '.stdout')).write_text(result.stdout)
        (out / (name + '.stderr')).write_text(result.stderr)
        commands.append({"command": argv, "exitCode": result.returncode})
        result.check_returncode()
        return result

    for side, headers, layouts in [('c', c_headers, c_structs),
                                   ('dawn', [dawn_header], {name: dawn_structs[name] for name in c_structs if name in dawn_structs})]:
        source = out / (side + '.c')
        source.write_text(probe_source(headers, layouts))
        executable = out / (side + ('-layout.exe' if os.name == 'nt' else '-layout'))
        prefix = [str(compiler), *args.compiler_arg, '-std=c11', '-I' + str(c_dir)]
        command([*prefix, str(source), '-o', str(executable)], side + '-compile')
        layout_output = command([str(executable)], side + '-layout').stdout
        rows: dict[str, dict[str, int]] = {}
        for line in layout_output.splitlines():
            name, field, value = line.split()
            rows.setdefault(name, {})[field] = int(value)
        observed[side] = rows
        ast_source = out / (side + '-declarations.c')
        ast_source.write_text('\n'.join('#include ' + json.dumps(str(header)) for header in headers) + '\n')
        ast = command([*prefix, '-fsyntax-only', '-Xclang', '-ast-dump=json', str(ast_source)], side + '-ast')
        declarations[side] = signatures(json.loads(ast.stdout))
    required = required_names(copied[args.selector.resolve()])
    proc_rows = [{"symbol": name, "c": declarations['c'].get(name), "pinnedDawn": declarations['dawn'].get(name),
                  "same": bool(declarations['c'].get(name)) and declarations['c'].get(name) == declarations['dawn'].get(name)}
                 for name in required]
    c_constants, dawn_constants = constants(c_text), constants(dawn_text)
    enum_failures = compare_constants(c_constants, dawn_constants)
    layout_failures = compare_layouts(observed['c'], observed['dawn'])
    required_records = {name for procedure in required for name in re.findall(
        r'\bWGPU\w+\b', declarations['dawn'].get(procedure, '')) if name in dawn_structs}
    layout_failures.extend({"struct": name, "reason": "selector-required struct has no complete C definition"}
                           for name in sorted(required_records - c_structs.keys()))
    drift = [name for name, expected in inputs.items() if not Path(name).is_file() or sha256(Path(name)) != expected]
    report = {"schemaVersion": 1, "scope": "CPU C descriptor layouts/constants and exact selector procedure declarations; no loaded-provider or GPU/browser qualification",
              "status": "blocked" if layout_failures or enum_failures or drift or any(not row['same'] for row in proc_rows) else "pass",
              "inputSha256": inputs, "inputDrift": drift, "compiler": {"path": str(compiler), "sha256": sha256(compiler),
              "version": command([str(compiler), '--version'], 'compiler-version').stdout},
              "host": {"platform": os.name, "machine": command([str(compiler), '-dumpmachine'], 'compiler-machine').stdout.strip()},
              "compilerEnvironment": {name: os.environ.get(name) for name in
                                      ('SDKROOT', 'DEVELOPER_DIR', 'CPATH', 'C_INCLUDE_PATH', 'LIB', 'INCLUDE')},
              "structures": observed, "layoutFailures": layout_failures, "constantCount": len(c_constants),
              "selectorRequiredStructs": sorted(required_records),
              "constantFailures": enum_failures, "procedures": proc_rows, "commands": commands}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, required=True, help='Actual pinned generated Dawn webgpu.h from the selected Chromium build.')
    parser.add_argument('--c-include', type=Path, default=ROOT / 'runtime/c/native/include')
    parser.add_argument('--base-header', type=Path, default=ROOT / 'runtime/zig/vendor/webgpu-headers/webgpu.h')
    parser.add_argument('--selector', type=Path, default=ROOT / 'browser/chromium/patches/dawn-doe-runtime.patch')
    parser.add_argument('--compiler', default='clang', help='Host Clang executable; exact binary/version retained.')
    parser.add_argument('--compiler-arg', action='append', default=[], help='Explicit additional compiler argument, repeated.')
    parser.add_argument('--timeout', type=float, default=60, help='Bound each CPU compiler/probe process.')
    parser.add_argument('--out', type=Path, required=True, help='Fresh retained raw-input/probe/result directory.')
    args = parser.parse_args()
    try:
        report = run(args)
    except FileExistsError:
        raise SystemExit('output directory already exists; preserve prior receipts and choose a fresh output')
    except (OSError, ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        failure = {"status": "blocked", "scope": "CPU ABI probe failed before comparison", "error": str(error)}
        if args.out.is_dir():
            (args.out / 'failure.json').write_text(json.dumps(failure, indent=2) + '\n')
        print(json.dumps(failure))
        return 1
    print(json.dumps({"status": report['status'], "structures": len(report['structures']['c']),
                      "layoutFailures": len(report['layoutFailures']), "constantFailures": len(report['constantFailures']),
                      "procedures": len(report['procedures']), "exactProcedures": sum(row['same'] for row in report['procedures'])}))
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
