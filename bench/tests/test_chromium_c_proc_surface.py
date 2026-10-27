"""Independent CPU resolver fixtures and mutated C ownership-contract controls."""

from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from bench.tools import chromium_c_proc_surface as checker


ROOT = Path(__file__).resolve().parents[2]
NAMES = ("wgpuCreateInstance", "wgpuInstanceRelease", "wgpuDeviceImportSharedBufferMemory")
FIXTURE = r'''
#include <stddef.h>
#include <string.h>
typedef struct {const char *data; size_t length;} View;
static int instance;
void *wgpuCreateInstance(void *descriptor) {(void)descriptor; return &instance;}
void wgpuInstanceRelease(void *value) {(void)value;}
void *wgpuDeviceImportSharedBufferMemory(void *device,void *descriptor) {(void)device;(void)descriptor;return NULL;}
void *wgpuSharedBufferMemoryCreateBuffer(void *a,void *b) {(void)a;(void)b;return NULL;}
unsigned wgpuSharedBufferMemoryBeginAccess(void *a,void *b,void *c) {(void)a;(void)b;(void)c;return 2;}
unsigned wgpuSharedBufferMemoryEndAccess(void *a,void *b,void *c) {(void)a;(void)b;(void)c;return 2;}
unsigned wgpuSharedBufferMemoryGetProperties(void *a,void *b) {(void)a;(void)b;return 2;}
void *wgpuGetProcAddress(View name) {
    if (name.length==(size_t)-1) name.length=strlen(name.data);
#define ENTRY(symbol) if(name.length==sizeof(#symbol)-1&&!memcmp(name.data,#symbol,name.length))return (void *)&symbol;
    ENTRY(wgpuCreateInstance)
    ENTRY(wgpuInstanceRelease)
#ifdef WRONG_REQUIRED
    if(name.length==sizeof("wgpuDeviceImportSharedBufferMemory")-1&&!memcmp(name.data,"wgpuDeviceImportSharedBufferMemory",name.length))return (void *)&wgpuCreateInstance;
#else
    ENTRY(wgpuDeviceImportSharedBufferMemory)
#endif
    return NULL;
}
'''


class CProcSurfaceTests(unittest.TestCase):
    def fixture(self, scratch: str, wrong: bool = False) -> tuple[Path, dict, Path]:
        root = Path(scratch)
        payload = json.loads((ROOT / "config/doe-chromium-proc-surface.c.json").read_text())
        patch = root / payload["requiredSelector"]["sourcePath"]
        patch.parent.mkdir(parents=True)
        patch.write_text("\n".join(f'+if (!load(&mDawnProcs.fixture{i}, "{name}")) {{' for i, name in enumerate(NAMES)))
        payload["requiredSelector"]["sourceSha256"] = hashlib.sha256(patch.read_bytes()).hexdigest()
        header = root / payload["wireProcTable"]["tableHeaderPath"]
        header.parent.mkdir(parents=True)
        header.write_text("\n".join(f"    WGPUProc{name[4:]} field{i} = nullptr;"
                                    for i, name in enumerate((*NAMES, "wgpuBufferCreateTexelView"))))
        for name in [payload["resolverSourcePath"], *payload["behaviorSources"].values()]:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        source = root / "fixture.c"
        source.write_text(FIXTURE)
        library = root / ("libwebgpu_doe_c.dylib" if os.uname().sysname == "Darwin" else "libwebgpu_doe_c.so")
        subprocess.run([shutil.which("cc"), "-shared", "-fPIC", "-DWRONG_REQUIRED" if wrong else "-DGOOD",
                        str(source), "-o", str(library)], check=True, capture_output=True)
        return root, payload, library

    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "CPU shared-library fixture needs POSIX C compiler")
    def test_actual_local_addresses_and_optional_null_slot_are_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root, payload, library = self.fixture(scratch)
            report = checker.check(payload, root=root, config_path_text="fixture", library_override=str(library))
            self.assertEqual(report["missingRequired"], [], report["checks"])
            self.assertEqual(sum(row["required"] for row in report["procedureInventory"]), 3)
            optional = next(row for row in report["procedureInventory"] if row["symbol"] == "wgpuBufferCreateTexelView")
            self.assertFalse(optional["available"])
            self.assertFalse(optional["required"])
            self.assertFalse(optional["local"])

    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "CPU shared-library fixture needs POSIX C compiler")
    def test_nonnull_required_pointer_to_different_export_rejects(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root, payload, library = self.fixture(scratch, wrong=True)
            report = checker.check(payload, root=root, config_path_text="fixture", library_override=str(library))
            self.assertIn("required_c_proc:wgpuDeviceImportSharedBufferMemory", report["missingRequired"])

    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "CPU shared-library fixture needs POSIX C compiler")
    def test_selector_drift_and_missing_required_header_declaration_reject(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root, payload, library = self.fixture(scratch)
            bad = copy.deepcopy(payload)
            bad["requiredSelector"]["sourceSha256"] = "0" * 64
            report = checker.check(bad, root=root, config_path_text="fixture", library_override=str(library))
            self.assertIn("c_surface_inputs", report["missingRequired"])
            header = root / payload["wireProcTable"]["tableHeaderPath"]
            header.write_text("    WGPUProcCreateInstance createInstance = nullptr;")
            report = checker.check(payload, root=root, config_path_text="fixture", library_override=str(library))
            self.assertIn("required_selector_header", report["missingRequired"])

    @unittest.skipUnless(os.name == "posix" and shutil.which("cc"), "CPU shared-library fixture needs POSIX C compiler")
    def test_accidental_zig_selection_cannot_satisfy_c_config(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root, payload, library = self.fixture(scratch)
            wrong = library.with_name("libwebgpu_doe_full" + library.suffix)
            library.rename(wrong)
            report = checker.check(payload, root=root, config_path_text="fixture", library_override=str(wrong))
            self.assertIn("c_surface_inputs", report["missingRequired"])
            self.assertTrue(any("v1 config for Zig" in row["message"] for row in report["checks"]))

    def test_foreign_mapping_and_unrecognized_resolver_return_reject(self) -> None:
        text = (ROOT / "runtime/c/native/src/procedures.c").read_text()
        self.assertIn("wgpuDeviceImportSharedBufferMemory", checker.local_symbols(text))
        for changed in [text.replace('(WGPUProc)wgpuDeviceImportSharedBufferMemory}', '(WGPUProc)wgpuCreateInstance}'),
                        text.replace('return NULL;', 'return foreign_provider(name);')]:
            with self.assertRaises(ValueError):
                checker.local_symbols(changed)

    def test_unsupported_buffer_success_or_error_object_marker_in_comment_cannot_pass(self) -> None:
        payload = json.loads((ROOT / "config/doe-chromium-proc-surface.c.json").read_text())
        sources = {key: (ROOT / name).read_text() for key, name in payload["behaviorSources"].items()}
        self.assertEqual(checker.shared_memory_source_failures(sources), [])
        bad = {**sources, "sharedBuffer": sources["sharedBuffer"].replace('return WGPUStatus_Error;', 'return WGPUStatus_Success;')}
        self.assertTrue(checker.shared_memory_source_failures(bad))
        bad = {**sources, "errorObjects": sources["errorObjects"].replace('doe_resource_buffer_error(', '/*doe_resource_buffer_error*/foreign_buffer(')}
        self.assertTrue(checker.shared_memory_source_failures(bad))


if __name__ == "__main__":
    unittest.main()
