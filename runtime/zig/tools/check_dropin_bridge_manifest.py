from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tools/bridge_manifests/dropin_bridge_manifest.json"


def check_source() -> list[str]:
    manifest = json.loads(MANIFEST.read_text())
    if manifest.get("schemaVersion") != 1:
        raise ValueError("Unsupported private drop-in bridge inventory version")
    owner = ROOT.parent.parent / manifest["owner"]
    source = " ".join(owner.read_text().split())
    exports = dict(re.findall(
        r'@export\(&([A-Za-z0-9_]+), \.\{ \.name = "([A-Za-z0-9_]+)", \.visibility = .*? \}\);',
        source,
    ))
    entries = manifest["symbols"]
    names = [entry["name"] for entry in entries]
    if len(set(names)) != len(names) or set(exports.values()) != set(names):
        raise ValueError("Private @export declarations and bridge inventory differ")
    for entry in entries:
        name = entry["name"]
        implementation = entry["implementation"]
        if exports.get(implementation) != name:
            raise ValueError(f"{name}: incorrect implementation owner")
        if (entry["callingConvention"] != "c"
                or entry["elfVisibility"] != "hidden"
                or entry["machOVisibility"] != "hidden"
                or entry["coffVisibility"] != "default"
                or entry["publicProcedureLookup"] is not False):
            raise ValueError(f"{name}: unsupported private bridge visibility/ABI")
        signature = (f"fn {implementation}(name: {entry['argumentType']}) "
                     f"callconv(.c) {entry['returnType']}")
        if signature not in source:
            raise ValueError(f"{name}: implementation C ABI differs from inventory")
        visibility = (f'@export(&{implementation}, .{{ .name = "{name}", '
                      '.visibility = if (@import("builtin").os.tag == .windows) '
                      '.default else .hidden });')
        if visibility not in source:
            raise ValueError(f"{name}: compiler visibility differs from inventory")
        declarations = []
        for path in (ROOT / "src/dropin").glob("*.zig"):
            text = path.read_text()
            if re.search(rf"(?:pub )?extern fn {name}\(", text):
                declarations.append(path.name)
            if re.search(r"const proc = wgpuGetProcAddress\(", text):
                raise ValueError(f"{path}: wrapper resolves through public lookup")
        if not declarations:
            raise ValueError(f"{name}: no ABI module consumes the private seam")
    return names


def output(arguments: list[str]) -> str:
    return subprocess.check_output(arguments, text=True)


def check_library(path: Path, file_format: str, names: list[str]) -> None:
    if file_format == "coff":
        symbols = set(re.findall(r"^\s*\[\s*\d+\]\s+(\S+)\s*$",
                                 output(["objdump", "-p", str(path)]), re.M))
        for name in names:
            if name not in symbols:
                raise ValueError(f"{path}: missing inventoried private COFF export {name}")
        return
    all_symbols = output(["nm", "--defined-only", str(path)]) if file_format == "elf" else output(["nm", str(path)])
    public_symbols = output(["nm", "-D", "--defined-only", str(path)]) if file_format == "elf" else output(["nm", "-gU", str(path)])
    for name in names:
        symbol = name if file_format == "elf" else "_" + name
        if not re.search(rf"\b{symbol}$", all_symbols, re.M):
            raise ValueError(f"{path}: missing internal bridge implementation {symbol}")
        if re.search(rf"\b{symbol}$", public_symbols, re.M):
            raise ValueError(f"{path}: private bridge unexpectedly publicly visible: {symbol}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate the private drop-in C bridge inventory and optional linked visibility")
    parser.add_argument("--library", type=Path)
    parser.add_argument("--format", choices=("elf", "macho", "coff"))
    args = parser.parse_args()
    if bool(args.library) != bool(args.format):
        parser.error("--library and --format must be supplied together")
    names = check_source()
    if args.library:
        check_library(args.library, args.format, names)
    print(f"PASS private drop-in bridge inventory: {len(names)} symbol(s)")


if __name__ == "__main__":
    main()
