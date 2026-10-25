#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Non-destructive preflight controls; actual relocation has a separate receipt."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("native_package", Path(__file__).with_name("build-native-package.py"))
assert SPEC is not None and SPEC.loader is not None
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class LeanProofRecordTests(unittest.TestCase):
    def test_records_zig_provenance_and_no_lean_verified_build(self) -> None:
        # The repository tree hashes to the provenance its proof artifact records.
        record = PACKAGE.lean_proof_record(Path(__file__).resolve().parents[3])
        self.assertIs(record["leanVerifiedBuild"], False)
        self.assertIsNone(record["proofArtifactSha256"])
        self.assertTrue(record["proofArtifactProvenanceMatches"])
        self.assertRegex(record["provenance"]["leanSourceTreeSha256"], r"^[0-9a-f]{64}$")
        self.assertTrue(str(record["provenance"]["leanToolchainRef"]).startswith("leanprover/lean4:v"))


class PackagePreflightTests(unittest.TestCase):
    def test_existing_paths_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            occupied = root / "occupied"
            occupied.mkdir()
            sentinel = occupied / "source"
            sentinel.write_text("preserve")
            for build, destination in ((occupied, root / "new"), (root / "new", occupied)):
                with self.assertRaises(FileExistsError):
                    PACKAGE.build_package(root, "HEAD", build, destination, "gcc", "g++")
                self.assertEqual(sentinel.read_text(), "preserve")
                self.assertFalse((root / "new").exists())

    def test_overlapping_paths_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "fresh"
            for destination in (build, build / "child"):
                with self.assertRaises(ValueError):
                    PACKAGE.build_package(root, "HEAD", build, destination, "gcc", "g++")
                self.assertFalse(build.exists())

    def test_dangling_links_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            link = root / "link"
            link.symlink_to(root / "absent")
            for build, destination in ((link, root / "stage"), (root / "build", link)):
                with self.assertRaises(FileExistsError):
                    PACKAGE.build_package(root, "HEAD", build, destination, "gcc", "g++")
                self.assertTrue(link.is_symlink())
                self.assertFalse((root / "absent").exists())

    def test_unsupported_host_has_no_side_effects(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.object(PACKAGE.platform, "system", return_value="Plan9"):
            root = Path(directory)
            with self.assertRaises(ValueError):
                PACKAGE.build_package(root, "HEAD", root / "build", root / "stage", "gcc", "g++")
            self.assertEqual(list(root.iterdir()), [])

    def test_windows_requires_explicit_sdk_before_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.object(PACKAGE.platform, "system", return_value="Windows"):
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "DXC SDK"):
                PACKAGE.build_package(root, "HEAD", root / "build", root / "stage", "gcc", "g++")
            self.assertEqual(list(root.iterdir()), [])

    def test_missing_revision_precedes_path_creation(self) -> None:
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(PACKAGE, "captured", side_effect=subprocess.CalledProcessError(128, "git")):
                with self.assertRaises(subprocess.CalledProcessError):
                    PACKAGE.build_package(root, "missing", root / "build", root / "stage", "gcc", "g++")
            self.assertEqual(list(root.iterdir()), [])


class PackageLoaderTests(unittest.TestCase):
    def test_windows_cannot_load_missing_dlls_from_build_path(self) -> None:
        environment = {"SystemRoot": "C:/Windows", "Path": "C:/old-build;C:/sdk"}
        with patch.dict(PACKAGE.os.environ, environment, clear=True):
            actual = PACKAGE.loader_environment("Windows")
        self.assertNotIn("Path", actual)
        self.assertEqual(actual["PATH"], str(Path("C:/Windows/System32")))

    def test_pe_dependencies_require_packaged_or_system_dlls(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            system = root / "Windows/System32"
            package.mkdir()
            system.mkdir(parents=True)
            (package / "webgpu_doe_c.dll").touch()
            (package / "doe_compiler.dll").touch()
            (system / "KERNEL32.dll").touch()
            valid = "    doe_compiler.dll\n    KERNEL32.dll\n    api-ms-win-crt-runtime-l1-1-0.dll\n"
            with patch.dict(PACKAGE.os.environ, {"SystemRoot": str(root / "Windows")}), \
                    patch.object(PACKAGE, "captured", return_value=valid):
                self.assertEqual(len(PACKAGE.audit_libraries(package, "Windows")), 2)
            for invalid in ("    missing.dll\n", "    C:\\build\\doe_compiler.dll\n", "no imports"):
                with self.subTest(imports=invalid), \
                        patch.dict(PACKAGE.os.environ, {"SystemRoot": str(root / "Windows")}), \
                        patch.object(PACKAGE, "captured", return_value=invalid):
                    with self.assertRaises(RuntimeError):
                        PACKAGE.audit_libraries(package, "Windows")

    def test_loader_overrides_cannot_rescue_relocation(self) -> None:
        environment = {"PATH": "/usr/bin", "LD_LIBRARY_PATH": "/old",
                       "LD_PRELOAD": "old.so", "DYLD_LIBRARY_PATH": "/old",
                       "DYLD_FALLBACK_LIBRARY_PATH": "/old",
                       "DYLD_INSERT_LIBRARIES": "old.dylib"}
        with patch.dict(PACKAGE.os.environ, environment, clear=True):
            self.assertEqual(PACKAGE.loader_environment(), {"PATH": "/usr/bin"})

    def macho_output(self, arguments: list[str]) -> str:
        command, option, filename = arguments
        self.assertEqual(command, "otool")
        name = Path(filename).name
        if option == "-D":
            return filename + ":\n@rpath/" + name
        if option == "-L":
            result = filename + ":\n\t@rpath/" + name + " (compatibility version 0.0.0)"
            if name == "libwebgpu_doe_c.dylib":
                result += "\n\t@rpath/libdoe_compiler.dylib (compatibility version 0.0.0)"
            return result + "\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)"
        if name == "libwebgpu_doe_c.dylib":
            return "Load command 3\n cmd LC_RPATH\n cmdsize 32\n path @loader_path (offset 12)"
        return "Load command 0\n cmd LC_SEGMENT_64"

    def test_relative_macho_dependency_closure(self) -> None:
        with patch.object(PACKAGE, "captured", side_effect=self.macho_output):
            self.assertEqual(len(PACKAGE.audit_libraries(Path("/build"), "Darwin")), 2)

    def test_macho_rejects_absolute_identity_dependency_and_runpath(self) -> None:
        for bad_option in ("-D", "-L", "-l"):
            def invalid(arguments: list[str]) -> str:
                output = self.macho_output(arguments)
                if arguments[1] != bad_option:
                    return output
                return output.replace("@rpath/", "/build/").replace("@loader_path", "/build")

            with self.subTest(option=bad_option), patch.object(PACKAGE, "captured", side_effect=invalid):
                with self.assertRaises(RuntimeError):
                    PACKAGE.audit_libraries(Path("/build"), "Darwin")

    def test_linux_requires_origin_runpath(self) -> None:
        with patch.object(PACKAGE, "captured", return_value="Library runpath: [$ORIGIN]"):
            PACKAGE.audit_libraries(Path("/build"), "Linux")
        with patch.object(PACKAGE, "captured", return_value="Library runpath: [/build]"):
            with self.assertRaises(RuntimeError):
                PACKAGE.audit_libraries(Path("/build"), "Linux")


if __name__ == "__main__":
    unittest.main()
