#!/usr/bin/env python3
"""Compare C ABI field sizes/offsets to pinned retained extern declarations.

No Zig compiler, generator, runtime linking, SDK download, or driver is used.
ctypes models the selected Linux LP64 ABI independently of the C declarations.
This does not establish the correctness of the retained ABI or physical execution.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[1]
    manifest = json.loads((owner / "ABI_PROVENANCE.json").read_text())
    source = args.repo / manifest["source_directory"]
    for name, digest in manifest["sha256"].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"pinned ABI source drift: {name}")
    types = {"u8": ctypes.c_uint8, "u32": ctypes.c_uint32,
             "i32": ctypes.c_int32, "u64": ctypes.c_uint64,
             "usize": ctypes.c_size_t, "f32": ctypes.c_float,
             "VkFlags": ctypes.c_uint32, "VkBool32": ctypes.c_uint32,
             "VkDeviceSize": ctypes.c_uint64, "VkStructureType": ctypes.c_int32}
    handles = (source / "vulkan_types.zig").read_text()
    for name, definition in re.findall(r"pub const (Vk\w+) = ([^;]+);", handles):
        if definition.startswith("?*"):
            types[name] = ctypes.c_void_p
        elif definition in types:
            types[name] = types[definition]
    selected = set(manifest["structs"])
    fields = {}
    for file in ("vk_structs.zig", "vk_capability_structs.zig"):
        for name, kind, body in re.findall(r"pub const (Vk\w+) = extern (struct|union) \{(.*?)\n\};", (source / file).read_text(), re.S):
            if name not in selected:
                continue
            members = []
            for line in body.strip().splitlines():
                line = line.split("//", 1)[0].strip().rstrip(",")
                if not line:
                    continue
                member, spelling = line.split(": ", 1)
                spelling = spelling.replace("vk.", "").removeprefix("?")
                if spelling.startswith(("*", "[*")):
                    value = ctypes.c_void_p
                else:
                    array = re.fullmatch(r"\[([^]]+)\](.*)", spelling)
                    if array:
                        length = {"VK_MAX_PHYSICAL_DEVICE_NAME_SIZE": "256", "VK_UUID_SIZE": "16"}.get(array[1], array[1])
                        value = types[array[2]] * int(length)
                    else:
                        value = types[spelling]
                members.append((member, value))
            types[name] = type(name, (ctypes.Structure if kind == "struct" else ctypes.Union,), {"_fields_": members})
            fields[name] = members
    lines = ['#include "vk_abi_functions.h"', '#include "vk_abi_constants.h"']
    if "vkCmdSetBlendConstants" in manifest.get("official_functions", {}):
        lines.extend([
            'void doe_vk_abi_blend_signature(doe_vk_functions *functions) {',
            '    void (*set_blend)(VkCommandBuffer, const float *) = functions->vkCmdSetBlendConstants;',
            '    (void)set_blend;',
            '}'
        ])
    assertions = 0
    for name, value in manifest.get("official_constants", {}).get("values", {}).items():
        lines.append(f'_Static_assert({name} == {value}, "{name} official constant");')
        assertions += 1
    for name, members in fields.items():
        native = types[name]
        lines.append(f'_Static_assert(sizeof({name}) == {ctypes.sizeof(native)}, "{name} size");')
        lines.append(f'_Static_assert(_Alignof({name}) == {ctypes.alignment(native)}, "{name} alignment");')
        assertions += 2
        for member, value in members:
            offset = getattr(native, member).offset
            lines.append(f'_Static_assert(offsetof({name}, {member}) == {offset}, "{name}.{member} offset");')
            lines.append(f'_Static_assert(sizeof((({name} *)0)->{member}) == {ctypes.sizeof(value)}, "{name}.{member} size");')
            assertions += 2
    with tempfile.TemporaryDirectory(prefix="doe-vk-abi-") as temp:
        check = Path(temp) / "check.c"
        check.write_text("\n".join(lines) + "\n")
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                        "-I", str(owner / "src"), "-c", str(check), "-o", str(Path(temp) / "check.o")], check=True)
    print(f"PASS pinned Linux LP64 ABI: {len(fields)} records, {assertions} field/size/alignment assertions; not driver qualification")


if __name__ == "__main__":
    main()
