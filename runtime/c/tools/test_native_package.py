#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Non-destructive preflight controls; actual relocation has a separate receipt."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import sys
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
    def test_all_abi_inventories_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            owner, destination = root / "owner", root / "package"
            owner.mkdir()
            destination.mkdir()
            names = ("EXPORTED_SYMBOLS.txt", "EXTENSION_SYMBOLS.txt",
                     "LEGACY_SYMBOLS.txt", "SHARED_MEMORY_SYMBOLS.txt")
            for index, name in enumerate(names):
                (owner / name).write_text(f"owned_symbol_{index}\n", encoding="utf-8")
            PACKAGE.copy_symbol_inventories(owner, destination)
            self.assertEqual({path.name for path in destination.iterdir()}, set(names))
            for name in names:
                self.assertEqual((owner / name).read_bytes(), (destination / name).read_bytes())
            (owner / names[-1]).unlink()
            with self.assertRaises(FileNotFoundError):
                PACKAGE.copy_symbol_inventories(owner, destination)

    def test_python_build_helpers_leave_captured_inputs_immutable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "inventory.py").write_text("names = ['actual_owner']\n", encoding="utf-8")
            (source / "build.py").write_text("import inventory\nprint(inventory.names)\n", encoding="utf-8")
            before = {p.name: p.read_bytes() for p in source.iterdir()}
            PACKAGE.execute([sys.executable, str(source / "build.py")], root / "build.log")
            self.assertEqual({p.name: p.read_bytes() for p in source.iterdir()}, before)
            self.assertIn("actual_owner", (root / "build.log").read_text())

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

    @staticmethod
    def elf_output(arguments: list[str], native: str = "", exports: str = "0010 T wgpuCreateInstance\n") -> str:
        name = Path(arguments[-1]).name
        if arguments[0] == "nm":
            return exports
        if name == "libwebgpu_doe_c.so":
            return native or ("Library runpath: [$ORIGIN]\nShared library: [libdoe_compiler.so]\n"
                              "Shared library: [libc.so.6]\nLibrary soname: [libwebgpu_doe_c.so]")
        return "Shared library: [libm.so.6]\nLibrary soname: [libdoe_compiler.so]"

    def test_linux_requires_origin_runpath_names_and_function_exports(self) -> None:
        with patch.object(PACKAGE, "captured", side_effect=self.elf_output):
            self.assertEqual(len(PACKAGE.audit_libraries(Path("/build"), "Linux")), 2)
        valid = self.elf_output(["readelf", "-d", "libwebgpu_doe_c.so"])
        for native in (valid.replace("$ORIGIN", "/build"),
                       valid.replace("Library soname: [libwebgpu_doe_c.so]", ""),
                       valid.replace("[libdoe_compiler.so]", "[/build/libdoe_compiler.so]")):
            with self.subTest(native=native), patch.object(
                    PACKAGE, "captured", side_effect=lambda a, n=native: self.elf_output(a, native=n)):
                with self.assertRaises(RuntimeError):
                    PACKAGE.audit_libraries(Path("/build"), "Linux")
        data = "0010 T wgpuCreateInstance\n0020 D exported_table\n"
        with patch.object(PACKAGE, "captured", side_effect=lambda a: self.elf_output(a, exports=data)):
            with self.assertRaisesRegex(RuntimeError, "non-function"):
                PACKAGE.audit_libraries(Path("/build"), "Linux")

    def test_glibc_floor_refuses_newer_symbol_versions(self) -> None:
        symbols = {"a.so": "DF *UND* GLIBC_2.2.5 memcpy\nDF *UND* (GLIBC_2.34) dlopen\nGLIBC_PRIVATE\n",
                   "b.so": "DF *UND* (GLIBC_2.29) exp\n"}
        with patch.object(PACKAGE, "captured", side_effect=lambda a: symbols[Path(a[-1]).name]):
            self.assertEqual(PACKAGE.audit_glibc_floor(Path("/build"), ("a.so", "b.so"), "2.34"),
                             {"a.so": "2.34", "b.so": "2.29"})
            with self.assertRaisesRegex(RuntimeError, "GLIBC_2.34, above the GLIBC_2.31"):
                PACKAGE.audit_glibc_floor(Path("/build"), ("a.so",), "2.31")
        with patch.object(PACKAGE, "captured", return_value="no versions"):
            with self.assertRaises(RuntimeError):
                PACKAGE.audit_glibc_floor(Path("/build"), ("a.so",), "2.34")

    def test_glibc_floor_is_linux_major_minor_before_mutation(self) -> None:
        for system, floor in (("Darwin", "2.34"), ("Linux", "2.34.1"), ("Linux", "latest")):
            with self.subTest(system=system, floor=floor), tempfile.TemporaryDirectory() as directory, \
                    patch.object(PACKAGE.platform, "system", return_value=system):
                root = Path(directory)
                with self.assertRaisesRegex(ValueError, "glibc-floor"):
                    PACKAGE.build_package(root, "HEAD", root / "build", root / "stage", "gcc", "g++",
                                          glibc_floor=floor)
                self.assertEqual(list(root.iterdir()), [])

    def test_zig_compiler_must_match_pinned_toolchain(self) -> None:
        source = Path(__file__).resolve().parents[3]
        pinned = json.loads((source / "config/toolchains.json").read_text())["toolchains"]["zig"]["version"]
        self.assertIsNone(PACKAGE.zig_version(["gcc"], source))
        with patch.object(PACKAGE, "captured", return_value=pinned) as call:
            self.assertEqual(PACKAGE.zig_version(["/opt/zig/zig", "cc", "-target", "x"], source), pinned)
            self.assertEqual(call.call_args.args[0], ["/opt/zig/zig", "version"])
        with patch.object(PACKAGE, "captured", return_value="0.0.1"):
            with self.assertRaisesRegex(RuntimeError, "pinned zig"):
                PACKAGE.zig_version(["zig", "cc"], source)

    def test_compiler_version_drops_zig_target_arguments(self) -> None:
        with patch.object(PACKAGE, "captured", return_value="clang version 20\nTarget: x") as call:
            self.assertEqual(PACKAGE.compiler_version(["zig", "c++", "-target", "x86_64-linux-gnu.2.34"]),
                             "clang version 20")
            self.assertEqual(call.call_args.args[0], ["zig", "c++", "--version"])
            PACKAGE.compiler_version(["gcc-14", "-m64"])
            self.assertEqual(call.call_args.args[0], ["gcc-14", "-m64", "--version"])


if __name__ == "__main__":
    unittest.main()
