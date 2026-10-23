#!/usr/bin/env python3
"""Audit all compiler-owned object data and linked read-only relocation storage."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def output(command: list[str]) -> str:
    return subprocess.check_output(command, text=True)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_root.resolve()
    receipt = json.loads(args.build_receipt.read_text())
    assert receipt["status"] == "PASS"
    assert Path(receipt["build_root"]).resolve() == build
    for artifact in receipt["artifacts"].values():
        assert sha(Path(artifact["path"])) == artifact["sha256"]
    objects = sorted(build.glob("*.o"))
    assert objects, "No source objects to audit"
    data = []
    for obj in objects:
        table = output(["objdump", "-t", str(obj)])
        for line in output(["nm", "-S", "--defined-only", str(obj)]).splitlines():
            row = line.split()
            if len(row) != 4 or row[2].lower() not in ("b", "d", "g", "s", "c"):
                continue
            symbol = row[3]
            matching = [line for line in table.splitlines()
                        if line.split() and line.split()[-1] == symbol]
            assert len(matching) == 1, (obj, symbol, matching)
            fields = matching[0].split()
            section = fields[-3]
            assert section.startswith(".data.rel.ro"), (
                "Mutable compiler-owned data/BSS/common symbol", obj, symbol, section)
            data.append({"object": obj.name, "name": symbol, "section": section,
                         "size": int(row[1], 16), "objdump": matching[0]})
    shared = Path(receipt["artifacts"]["libdoe-m11.so"]["path"])
    programs = output(["readelf", "-lW", str(shared)])
    relro = []
    for line in programs.splitlines():
        fields = line.split()
        if fields and fields[0] == "GNU_RELRO":
            start, size = int(fields[2], 16), int(fields[5], 16)
            relro.append((start, start + size))
    assert relro, "Shared library lacks GNU_RELRO"
    linked_sections = []
    for line in output(["readelf", "-SW", str(shared)]).splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+"
                         r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
        if match and match[1].startswith(".data.rel.ro"):
            start, size = int(match[2], 16), int(match[3], 16)
            assert any(low <= start and start + size <= high for low, high in relro), (
                "Relocation table is not within read-only-after-relocation segment", line)
            linked_sections.append({"section": match[1], "start": start, "size": size})
    assert not data or linked_sections
    result = {
        "status": "PASS", "source_manifest_sha256": receipt["manifest_sha256"],
        "objects": {path.name: sha(path) for path in objects},
        "data_symbols": data, "mutable_data_or_bss_symbols": [],
        "linked_readonly_sections": linked_sections, "gnu_relro_ranges": relro,
        "allocator_imports": receipt["allocator_imports"],
        "scope": "All source-built compiler objects and linked relocation tables. ELF GNU_RELRO establishes post-load table protection; caller/libc/loader state excluded. Concurrent caller workspaces are checked separately."
    }
    assert not result["allocator_imports"]
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    print(json.dumps({"status": "PASS", "objects": len(objects),
                      "readonly_relocation_tables": len(data)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
