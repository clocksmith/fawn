#!/usr/bin/env python3
"""Run source-constructed CPU projection logic, never Windows SDK qualification.

The native class/base and helper bodies come from the supplied pinned Dawn tree.
Only the DWORD/LONG LUID words are modeled; the actual Windows SDK control is
windows_luid_abi.cpp and must be compiled separately on the official toolchain.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dawn-source", required=True, type=Path)
    parser.add_argument("--webgpu-header", required=True, type=Path)
    parser.add_argument("--chain-header", required=True, type=Path)
    parser.add_argument("--native-source", required=True, type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    source = args.dawn_source.resolve(strict=True)
    native = args.native_source.resolve(strict=True)
    header = args.webgpu_header.resolve(strict=True)
    chain = args.chain_header.resolve(strict=True)
    native_header = source / "include/dawn/native/D3DBackend.h"
    helper = source / "src/dawn/native/webgpu/InnerRuntimeDescriptor.h"
    backend = source / "src/dawn/native/webgpu/BackendWGPU.cpp"
    body = Path(__file__).with_name("windows-luid-projection-controls.inc")
    compilers = [Path(shutil.which(name) or name).resolve(strict=True) for name in [args.cc, args.cxx]]
    selected = [header, chain, native_header, helper, backend, body, native / "adapter_request.c",
                native / "adapter_request.h", *compilers, Path(__file__).resolve()]
    inputs = {str(path): digest(path) for path in selected}
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = {"scope": "Source-constructed CPU member-copy/C19 logic; no Windows SDK, GPU or browser qualification",
              "inputs": inputs, "passed": False, "commands": []}
    (out / "inputs.json").write_text(json.dumps(result, indent=2) + "\n")
    try:
        chain_type = re.search(r"    struct ChainedStruct \{.*?\n    \};", chain.read_text(), re.S)
        native_type = re.search(
            r"struct DAWN_NATIVE_EXPORT RequestAdapterOptionsLUID : wgpu::ChainedStruct \{.*?\n\};",
            native_header.read_text(), re.S)
        if not chain_type or not native_type:
            raise RuntimeError("pinned native LUID/base declaration shape changed")
        functions = helper.read_text()
        functions = functions[functions.index("inline bool InnerRuntimeConsumedDescriptor"):
                              functions.index("// TextureWGPU")]
        if functions.count("#if defined(_WIN32)") != 1 or functions.count("#endif") != 1:
            raise RuntimeError("Windows projection guard shape changed; inspect the actual helper")
        functions = functions.replace("#if defined(_WIN32)", "").replace("#endif  // defined(_WIN32)", "")
        functions = functions.replace("std::is_standard_layout_v<InnerRuntimeWindowsAdapterLuid>",
                                      "__is_standard_layout(InnerRuntimeWindowsAdapterLuid)")
        prelude = '''// Source-constructed logic only; no platform macros are spoofed.
#include "webgpu.h"
#include <stddef.h>
using uint32_t = __UINT32_TYPE__;
using int32_t = __INT32_TYPE__;
#define DAWN_NATIVE_EXPORT
struct LUID { uint32_t LowPart; int32_t HighPart; };
namespace wgpu {
enum class SType : uint32_t { RequestAdapterOptionsLUID = WGPUSType_RequestAdapterOptionsLUID };
CHAIN_TYPE
}
namespace dawn::native::d3d {
NATIVE_TYPE
RequestAdapterOptionsLUID::RequestAdapterOptionsLUID() {
    sType = wgpu::SType::RequestAdapterOptionsLUID;
}
}
namespace dawn::native::webgpu {
FUNCTIONS
}
#include "adapter_request.h"
using dawn::native::d3d::RequestAdapterOptionsLUID;
using namespace dawn::native::webgpu;
static_assert(sizeof(RequestAdapterOptionsLUID)==24 && alignof(RequestAdapterOptionsLUID)==8);
#if defined(_MSC_VER)
static_assert(__builtin_offsetof(RequestAdapterOptionsLUID,adapterLUID)==16);
#else
static_assert(__builtin_offsetof(RequestAdapterOptionsLUID,adapterLUID)==12);
#endif
static_assert(offsetof(InnerRuntimeWindowsAdapterLuid,adapterLUID)==offsetof(native_windows_adapter_luid,luid_low));
static_assert(sizeof(InnerRuntimeWindowsAdapterLuid)==sizeof(native_windows_adapter_luid));
static void require(bool value) { if (!value) __builtin_trap(); }
'''
        constructed = prelude.replace("CHAIN_TYPE", chain_type.group(0)).replace(
            "NATIVE_TYPE", native_type.group(0)).replace("FUNCTIONS", functions) + body.read_text()
        (out / "constructed.cpp").write_text(constructed)
        shutil.copy2(header, out / "webgpu.h")
        for name in ["adapter_request.c", "adapter_request.h"]:
            shutil.copy2(native / name, out / name)
        backend_text = backend.read_text()
        loop_start = backend_text.index("#if defined(_WIN32)\n        size_t chainDiagnosticBudget")
        loop_end = backend_text.index("\n    }\n    WGPUAdapter innerAdapter", loop_start)
        loop = backend_text[loop_start:loop_end]
        loop = loop.replace("#if defined(_WIN32)", "").replace("#endif", "")
        diagnostics = r'''#include "webgpu.h"
#include <cstddef>
#include <cassert>
#include <vector>
static std::vector<uint32_t> loggedTypes;
namespace dawn {
struct Log {
    Log& operator<<(const char*) { return *this; }
    Log& operator<<(uint32_t value) { loggedTypes.push_back(value); return *this; }
};
inline Log InfoLog() { return {}; }
}
PREDICATE
static void logChain(WGPUChainedStruct* head) {
    WGPURequestAdapterOptions innerAdapterOption=WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    innerAdapterOption.nextInChain=head;
LOOP
    assert(innerAdapterOption.nextInChain==head);
}
int main() {
    WGPUChainedStruct unknown{reinterpret_cast<WGPUChainedStruct*>(uintptr_t{1}),static_cast<WGPUSType>(0x6ffffff0)};
    logChain(&unknown);
    assert(loggedTypes.size()==1 && loggedTypes[0]==0x6ffffff0);
    assert(unknown.next==reinterpret_cast<WGPUChainedStruct*>(uintptr_t{1}));
    loggedTypes.clear();
    WGPUDawnTogglesDescriptor toggles=WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
    toggles.chain.next=&toggles.chain;
    logChain(&toggles.chain);
    assert(loggedTypes.size()==32 && toggles.chain.next==&toggles.chain);
    loggedTypes.clear();toggles.chain.next=nullptr;
    WGPUChainedStruct projected{nullptr,WGPUSType_RequestAdapterOptionsLUID};
    logChain(&projected);
    assert(loggedTypes.size()==1 && loggedTypes[0]==WGPUSType_RequestAdapterOptionsLUID && !projected.next);
    loggedTypes.clear();projected.next=&unknown;
    logChain(&projected);
    assert(loggedTypes.size()==2 && loggedTypes[0]==WGPUSType_RequestAdapterOptionsLUID &&
           loggedTypes[1]==0x6ffffff0 && projected.next==&unknown &&
           unknown.next==reinterpret_cast<WGPUChainedStruct*>(uintptr_t{1}));
    loggedTypes.clear();projected.next=&toggles.chain;
    logChain(&projected);
    assert(loggedTypes.size()==2 && loggedTypes[0]==WGPUSType_RequestAdapterOptionsLUID &&
           loggedTypes[1]==WGPUSType_DawnTogglesDescriptor && projected.next==&toggles.chain && !toggles.chain.next);
    return 0;
}
'''
        predicate = functions[:functions.index("inline WGPUChainedStruct* InnerRuntimeDescriptorChain")]
        (out / "diagnostics.cpp").write_text(diagnostics.replace("PREDICATE", predicate).replace("LOOP", loop))
        commands = [
            [str(compilers[0]), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-c", str(out / "adapter_request.c"), "-o", str(out / "parser.o")],
            [str(compilers[1]), "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-invalid-" + "offsetof",
             str(out / "constructed.cpp"), str(out / "parser.o"), "-o", str(out / "control")],
            [str(out / "control")],
            [str(compilers[1]), "-std=c++20", "-Wall", "-Wextra", "-Werror",
             str(out / "diagnostics.cpp"), "-o", str(out / "diagnostics")],
            [str(out / "diagnostics")],
        ]
        for index, command in enumerate(commands):
            result["commands"].append(command)
            with (out / f"control-{index}.log").open("wb") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60, check=True)
        result["constructedSha256"] = digest(out / "constructed.cpp")
        result["binarySha256"] = digest(out / "control")
        result["sourceDrift"] = [name for name, value in inputs.items() if digest(Path(name)) != value]
        result["passed"] = not result["sourceDrift"]
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "out": str(out), "error": result.get("error")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
