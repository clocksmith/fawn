"""Audit the C provider against the actual forced-selector load contract.

Name/local-address and source audits are CPU diagnostics. They do not establish
descriptor ABI, supported GPU features, presentation, or browser qualification.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
from typing import Any

import jsonschema

from runtime.c.native.tests.browser_required_procedures import required_names


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "config/doe-chromium-proc-surface.c.schema.json"
ENTRY = re.compile(r'\{"(wgpu\w+)",\s*\(WGPUProc\)(wgpu\w+)\}')


def address_image(address: int | None) -> Path | None:
    """Ask the OS which loaded image contains this actual procedure address."""
    if not address:
        return None
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        module = ctypes.c_void_p()
        get_module = kernel.GetModuleHandleExW
        get_module.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        get_module.restype = ctypes.c_int
        if not get_module(6, ctypes.c_void_p(address), ctypes.byref(module)):
            return None
        name = ctypes.create_unicode_buffer(32768)
        get_name = kernel.GetModuleFileNameW
        get_name.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
        get_name.restype = ctypes.c_uint32
        return Path(name.value) if get_name(module, name, len(name)) else None

    class DlInfo(ctypes.Structure):
        _fields_ = [("filename", ctypes.c_char_p), ("base", ctypes.c_void_p),
                    ("symbol", ctypes.c_char_p), ("symbolAddress", ctypes.c_void_p)]

    loader = ctypes.CDLL(None)
    lookup = loader.dladdr
    lookup.argtypes = [ctypes.c_void_p, ctypes.POINTER(DlInfo)]
    lookup.restype = ctypes.c_int
    info = DlInfo()
    return Path(os.fsdecode(info.filename)) if lookup(address, ctypes.byref(info)) and info.filename else None


def validate_config(payload: dict[str, Any]) -> list[str]:
    schema = json.loads(SCHEMA.read_text())
    return [error.message for error in jsonschema.Draft202012Validator(schema).iter_errors(payload)]


def function_body(text: str, name: str) -> str:
    """Extract a definition after removing comments; declarations cannot pass."""
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', text)
    if not match:
        raise ValueError(f"C function definition missing: {name}")
    depth = 1
    cursor = match.end()
    start = cursor
    while cursor < len(text) and depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    if depth:
        raise ValueError(f"unbalanced C function: {name}")
    return text[start:cursor - 1]


def local_symbols(text: str) -> set[str]:
    entries = ENTRY.findall(text)
    if not entries or any(name != target for name, target in entries):
        raise ValueError("C resolver must map each name to its own public export")
    symbols = {name for name, _ in entries}
    if len(symbols) != len(entries):
        raise ValueError("duplicate C resolver entry")
    body = function_body(text, "wgpuGetProcAddress")
    returns = re.findall(r'\breturn\s+([^;]+);', body)
    if not returns or set(returns) != {"NULL", "procedures[i].procedure"}:
        raise ValueError("C resolver return path is not exclusively local table or NULL")
    if not all(marker in body for marker in ("memcmp(", "procedures[i].name", "native_string_size(name, &size)")):
        raise ValueError("C resolver no longer checks exact borrowed name bytes")
    return symbols


def shared_memory_source_failures(sources: dict[str, str]) -> list[str]:
    failures = []
    expectations = {
        "errorObjects": {
            "wgpuDeviceCreateErrorBuffer": ("invalid_buffer", "true"),
            "invalid_buffer": ("doe_resource_buffer_error",),
            "wgpuDeviceCreateErrorTexture": ("invalid_texture", "true"),
            "invalid_texture": ("doe_resource_texture_error",),
        },
        "sharedTexture": {
            "wgpuDeviceImportSharedTextureMemory": ("doe_mtl_shared_surface_open",),
            "wgpuSharedTextureMemoryCreateTexture": ("doe_mtl_shared_texture_import", "doe_resource_texture_create"),
            "wgpuSharedTextureMemoryBeginAccess": ("SHARED_ACTIVE", "doe_mtl_shared_event_wait", "WGPUStatus_Error"),
            "wgpuSharedTextureMemoryEndAccess": ("completion_fence", "SHARED_IDLE", "WGPUStatus_Error"),
        },
        "sharedBuffer": {
            "wgpuDeviceImportSharedBufferMemory": ("return NULL;",),
            "wgpuSharedBufferMemoryCreateBuffer": ("return NULL;",),
            "wgpuSharedBufferMemoryBeginAccess": ("return WGPUStatus_Error;",),
            "wgpuSharedBufferMemoryEndAccess": ("return WGPUStatus_Error;", "state->initialized = WGPU_FALSE", "state->fenceCount = 0"),
            "wgpuSharedBufferMemoryGetProperties": ("return WGPUStatus_Error;", "properties->size = 0", "properties->usage = 0"),
        },
    }
    for owner, definitions in expectations.items():
        for symbol, markers in definitions.items():
            try:
                body = function_body(sources[owner], symbol)
                if any(marker not in body for marker in markers):
                    failures.append(f"{symbol}: owning C implementation contract changed")
                if owner == "sharedBuffer" and re.search(r'\breturn\s+(?!NULL\s*;|WGPUStatus_Error\s*;)', body):
                    failures.append(f"{symbol}: unsupported buffer path can return success")
            except ValueError as error:
                failures.append(str(error))
    return failures


def unsupported_buffer_controls(library: Any) -> list[str]:
    """Exercise only null-handle CPU rejection paths; create no GPU device."""
    failures = []
    signatures = {
        "wgpuDeviceImportSharedBufferMemory": (2, ctypes.c_void_p, None),
        "wgpuSharedBufferMemoryCreateBuffer": (2, ctypes.c_void_p, None),
        "wgpuSharedBufferMemoryBeginAccess": (3, ctypes.c_uint32, 2),
        "wgpuSharedBufferMemoryEndAccess": (3, ctypes.c_uint32, 2),
        "wgpuSharedBufferMemoryGetProperties": (2, ctypes.c_uint32, 2),
    }
    for name, (count, result_type, expected) in signatures.items():
        function = getattr(library, name)
        function.argtypes = [ctypes.c_void_p] * count
        function.restype = result_type
        if function(*([None] * count)) != expected:
            failures.append(name)
    return failures


def check(payload: dict[str, Any], *, root: Path, config_path_text: str,
          library_override: str = "", **_legacy_callbacks: Any) -> dict[str, Any]:
    from bench.tools import check_doe_chromium_proc_surface as legacy

    checks = []
    inputs: dict[str, str] = {}
    inventory = []

    def add(name: str, ok: bool, message: str, required: bool = True) -> None:
        checks.append(legacy.check_row(name, "pass" if ok else "fail", message, required=required))

    def source(name: str) -> bytes:
        path = legacy.resolve_path(root, name)
        if path is None:
            raise ValueError(f"invalid source path: {name}")
        data = path.read_bytes()
        inputs[str(path.resolve())] = hashlib.sha256(data).hexdigest()
        return data

    errors = validate_config(payload)
    for index, error in enumerate(errors):
        add(f"config:{index}", False, error)
    library_text = library_override or payload.get("libraryPath", "")
    if not errors:
        try:
            selector = payload["requiredSelector"]
            patch = legacy.resolve_path(root, selector["sourcePath"])
            patch_bytes = source(selector["sourcePath"])
            if hashlib.sha256(patch_bytes).hexdigest() != selector["sourceSha256"]:
                raise ValueError("owning forced-selector patch differs from the configured hash")
            names = required_names(patch)
            header = legacy.resolve_path(root, payload["wireProcTable"]["tableHeaderPath"])
            source(payload["wireProcTable"]["tableHeaderPath"])
            generated, error = legacy.parse_dawn_proc_table_symbols(header)
            if error:
                raise ValueError(error)
            missing_header = set(names) - set(generated)
            add("required_selector_header", not missing_header,
                f"{len(names)} selector-required loads; missing generated declarations: {sorted(missing_header)}")
            local = local_symbols(source(payload["resolverSourcePath"]).decode())
            add("local_c_resolver_source", True, "C resolver returns exact self-mapped table exports or NULL; source audit only")
            sources = {key: source(value).decode() for key, value in payload["behaviorSources"].items()}
            failures = shared_memory_source_failures(sources)
            add("c_shared_memory_source", not failures, "; ".join(failures) or
                "C error-object/IOSurface ownership paths present; shared buffer imports reject; source audit only")
            library_path = legacy.resolve_path(root, library_text)
            if library_path is None:
                raise ValueError("invalid selected C library path")
            if library_path.name not in ("libwebgpu_doe_c.dylib", "libwebgpu_doe_c.so", "webgpu_doe_c.dll"):
                raise ValueError("C configuration requires the explicit C provider library; use the v1 config for Zig")
            library_bytes = library_path.read_bytes()
            inputs[str(library_path.resolve())] = hashlib.sha256(library_bytes).hexdigest()
            library, error = legacy.load_ctypes_library(library_path.resolve())
            if error:
                raise ValueError(error)
            lookup = library.wgpuGetProcAddress
            lookup.argtypes = [legacy.WGPUStringView]
            lookup.restype = ctypes.c_void_p
            for name in sorted(set(generated) | set(names)):
                encoded = name.encode("ascii")
                address = lookup(legacy.WGPUStringView(encoded, len(encoded)))
                terminated = lookup(legacy.WGPUStringView(encoded, ctypes.c_size_t(-1).value))
                exported = getattr(library, name, None)
                direct = ctypes.cast(exported, ctypes.c_void_p).value if exported else None
                image = address_image(address)
                selected_image = image is not None and os.path.samefile(image, library_path)
                is_local = bool(address and name in local and address == direct and address == terminated and selected_image)
                mandatory = name in names
                inventory.append({"symbol": name, "required": mandatory, "available": bool(address),
                                  "exported": bool(direct), "local": is_local,
                                  "loadedImage": str(image) if image else None})
                if mandatory:
                    add("required_c_proc:" + name, is_local,
                        "explicit and terminated names resolve to the selected C export" if is_local else
                        "required selector proc is absent, foreign, or inconsistent")
            unknown = b"wgpuDefinitelyUnknownFawnProcedure"
            add("unknown_proc_rejects", not lookup(legacy.WGPUStringView(unknown, len(unknown))),
                "unknown procedure must return NULL")
            failures = unsupported_buffer_controls(library)
            add("unsupported_buffer_cpu_controls", not failures,
                "CPU-only null-handle imports/create/access/property queries reject; failures=" + str(failures))
            ok, message = legacy.bootstrap_ctypes_instance(library)
            add("instance_bootstrap", ok, message)
        except (OSError, ValueError, AttributeError) as error:
            add("c_surface_inputs", False, str(error))
    drift = [name for name, expected in inputs.items()
             if not Path(name).is_file() or hashlib.sha256(Path(name).read_bytes()).hexdigest() != expected]
    if drift:
        add("input_drift", False, "selected inputs changed during CPU check: " + str(drift))
    missing = [row["checkId"] for row in checks if row["required"] and row["status"] != "pass"]
    return {"schemaVersion": 2, "artifactKind": "doe_chromium_proc_surface_check",
            "providerImplementation": "c", "configPath": config_path_text,
            "libraryPath": library_text, "status": "blocked" if missing else "pass",
            "checks": checks, "missingRequired": missing, "procedureInventory": inventory,
            "inputSha256": inputs, "inputDrift": drift,
            "scope": "selector-required local names and CPU bootstrap/source audits only; no ABI, GPU feature or browser qualification"}
