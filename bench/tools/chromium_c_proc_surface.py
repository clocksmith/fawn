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
ACCESS_SOURCE_FILES = {
    "sharedQueue": "queue.c",
    "metalProgress": "metal_progress.c",
    "asyncCompletion": "async_completion.c",
    "metalShared": "../../backends/metal/src/shared_memory.c",
}


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
    """Extract executable definition text; comments/literals cannot pass."""
    text = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                  r"'(?:\\.|[^'\\])*'", ' ', text, flags=re.S)
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
            "wgpuDeviceImportSharedTextureMemory": ("doe_mtl_shared_surface_open_info(",),
            "wgpuSharedTextureMemoryCreateTexture": ("doe_mtl_shared_texture_import", "doe_resource_texture_create"),
            "wgpuSharedTextureMemoryBeginAccess": ("SHARED_ACTIVE", "WGPUStatus_Error"),
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
    return failures + access_batch_source_failures(sources)


def access_batch_source_failures(sources: dict[str, str]) -> list[str]:
    """Audit retained dependency wiring, not GPU execution or lease lifetimes.

    These deliberately narrow structural checks use the existing C definition
    extractor. Calls must carry the real source dependency and stay behind the
    existing success/acceptance guards; mentioning a symbol cannot satisfy them.
    Runtime shared-memory fault controls remain the behavioral proof.
    """
    definitions = {
        "begin": ("sharedTexture", "wgpuSharedTextureMemoryBeginAccess"),
        "end": ("sharedTexture", "wgpuSharedTextureMemoryEndAccess"),
        "accepted": ("sharedTexture", "native_metal_access_batch_accepted"),
        "flush": ("sharedQueue", "native_flush_access_batch"),
        "submit": ("sharedQueue", "submit_pending"),
        "record": ("metalProgress", "native_metal_record_batch"),
        "workDone": ("asyncCompletion", "wgpuQueueOnSubmittedWorkDone"),
        "wait": ("metalShared", "doe_mtl_commands_wait_shared_events"),
        "replay": ("metalShared", "doe_mtl_commands_replay_shared_waits"),
    }
    bodies = {}
    failures = []
    for role, (owner, name) in definitions.items():
        try:
            bodies[role] = re.sub(r"\s+", "", function_body(sources[owner], name))
        except (KeyError, ValueError) as error:
            failures.append(f"shared dependency source missing: {name}: {error}")
    if failures:
        return failures

    def require(role: str, pattern: str, obligation: str) -> None:
        if not re.search(pattern, bodies[role]):
            failures.append(f"shared dependency {role}: {obligation}")

    def forbid(role: str, pattern: str, obligation: str) -> None:
        if re.search(pattern, bodies[role]):
            failures.append(f"shared dependency {role}: {obligation}")

    require("begin", r"status=doe_mtl_commands_wait_shared_events\("
            r"command\.value\.metal,dependencies,count,&detail\);"
            r"failure=native_failure_metal\(status,&detail\);",
            "encode every producer wait and propagate admission failure")
    require("begin", r"if\(failure\.status==NATIVE_OK\)\{"
            r"device->access_batch=command;device->access_batch_memory=memory;\}",
            "retain the admitted dependency recording before returning")
    require("end", r"if\(admitted&&device->access_batch\.kind==WGPUBackendType_Metal&&"
            r"device->access_batch_memory==memory\)failure=native_flush_access_batch\(device\);",
            "flush producer waits even for a no-work End without output state")
    require("end", r"boolsuccess=admitted&&failure\.status==NATIVE_OK;"
            r"if\(success\)\{.*memory->access=SHARED_IDLE;memory->active_texture=NULL;\}",
            "failed End preserves active access and initialization state")
    require("flush", r"failure=native_failure_metal\("
            r"doe_mtl_commands_replay_shared_waits\(commands\.value\.metal,"
            r"device->access_batch\.value\.metal,&detail\),&detail\);.*"
            r"if\(failure\.status==NATIVE_OK\)return"
            r"submit_backend_commands\(device,commands,false,true\);",
            "submit a replay and keep the original until accepted")
    forbid("flush", r"native_release_access_batch\(device\)|"
           r"native_command_release\(&device->access_batch\)|device->access_batch=",
           "failed or unaccepted replay must retain the original waits")
    if (bodies["flush"].count("native_take_access_batch(device)") != 1 or
            "returnnative_submit_backend_commands_async(device,"
            "native_take_access_batch(device));" not in bodies["flush"]):
        failures.append("shared dependency flush: Metal must replay, not take original waits")
    require("record", r"doe_mtl_commands_replay_shared_waits\(native->value\.metal,"
            r"device->access_batch\.value\.metal,&detail\).*"
            r"failure\.status==NATIVE_OK&&i<count.*native_record_resource_commands\(",
            "replay producer waits before resource commands and retries")
    for role in ("record", "begin"):
        forbid(role, r"native_(?:take|release)_access_batch\(device\)",
               "must not drop the original dependency on recording failure")
    require("submit", r"link_pending\(device,pending\);"
            r"native_failurefailure=native_command_submit\(&pending->native\);",
            "pending owner adopts submission before uncertain completion")
    require("submit", r"if\(failure\.status==NATIVE_OK&&access_waits\)\{"
            r"doe_mtl_statusaccepted=doe_mtl_command_submission_status\("
            r"pending->native\.value\.metal\);"
            r"failure=accepted==DOE_MTL_BUSY\?native_failure_status\(NATIVE_UNKNOWN\):"
            r"native_failure_status\(native_from_metal\(accepted\)\);"
            r"if\(failure\.status==NATIVE_OK\)"
            r"failure=native_metal_access_batch_accepted\(device\);\}",
            "unknown or failed acceptance must keep the original dependency")
    for role in ("end", "record", "submit"):
        forbid(role, r"native_(?:take|release)_access_batch\(device\)|"
               r"native_command_release\(&device->access_batch\)|"
               r"device->access_batch=",
               "only the accepted queue-point owner may release original waits")
    if bodies["end"].count("memory->access=SHARED_IDLE;") != 1:
        failures.append("shared dependency end: access may close outside successful End")
    if bodies["submit"].count("native_metal_access_batch_accepted(device)") != 1:
        failures.append("shared dependency submit: acceptance must have one guarded owner")
    require("accepted", r"if\(status!=DOE_MTL_OK&&status!=DOE_MTL_UNSUPPORTED\)"
            r"returnnative_failure_metal\(status,NULL\);.*"
            r"native_release_access_batch\(device\);",
            "publish an accepted queue point before releasing original waits")
    require("workDone", r"native_flush_access_batch\(device\).*"
            r"native_queue_settlementpolled=native_poll_pending\(device\);.*"
            r"value->failure=.*flushed;",
            "flush before capturing WorkDone and propagate failure")
    require("wait", r"commands->shared_wait_events=retained;"
            r"commands->shared_wait_count=count;.*"
            r"native->commands_wait_event\(native->userdata,commands->native,"
            r"retained\[i\]\.event,retained\[i\]\.value,&detail\);"
            r"returndoe_mtl_native_record_result\(commands,status,&detail,error\);",
            "retain every wait lease before native encoding and failure")
    require("wait", r"status=doe_mtl_shared_event_import\(commands->context,"
            r"waits\[acquired\]\.event,&retained\[acquired\]\.event,&detail\);"
            r"if\(status==DOE_MTL_OK\)\{"
            r"retained\[acquired\]\.value=waits\[acquired\]\.value;acquired\+\+;\}",
            "acquire actual event leases and preserve each producer value")
    require("replay", r"returndoe_mtl_commands_wait_shared_events\(commands,"
            r"source->shared_wait_events,source->shared_wait_count,error\);",
            "replay the original event identities and values")
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
            shared_directory = Path(payload["behaviorSources"]["sharedTexture"]).parent
            sources.update({key: source(str(
                (root / shared_directory / relative).resolve()
                .relative_to(root.resolve()))).decode()
                for key, relative in ACCESS_SOURCE_FILES.items()})
            failures = shared_memory_source_failures(sources)
            add("c_shared_memory_source", not failures, "; ".join(failures) or
                "C error-object/IOSurface dependency wiring present; shared buffer imports reject; "
                "source audit only, not runtime lifetime proof")
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
