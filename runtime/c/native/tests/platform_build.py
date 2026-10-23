#!/usr/bin/env python3
"""Host controls for explicit link recipes and symbol normalization, not cross-execution."""
from pathlib import Path
import runpy
import subprocess
import tempfile
from unittest.mock import patch


def main() -> None:
    owner = Path(__file__).resolve().parents[1]
    exports = runpy.run_path(str(owner / "tests/audit-exports.py"))["native_exports"]
    with patch("subprocess.check_output", return_value="_wgpuCreateInstance\n__private\n") as call:
        assert exports(Path("fixture.dylib"), "Darwin") == {"wgpuCreateInstance", "_private"}
        assert call.call_args.args[0] == ["nm", "-gjU", "fixture.dylib"]
    with patch("subprocess.check_output", return_value="0010 T wgpuCreateInstance\n0020 T private\n") as call:
        assert exports(Path("fixture.so"), "Linux") == {"wgpuCreateInstance", "private"}
        assert call.call_args.args[0] == ["nm", "-D", "--defined-only", "fixture.so"]
    with tempfile.TemporaryDirectory(prefix="doe-native-platform-") as directory:
        # Existing empty dependency is never loaded or linked: make -n only.
        library = Path(directory) / "libcompiler_probe.dylib"
        library.touch()
        command = ["make", "--no-print-directory", "-n", "-C", str(owner),
                   f"BUILD={directory}", f"COMPILER_LIBRARY={library}"]
        darwin = subprocess.check_output(command + ["SHARED_PLATFORM=Darwin", "all"], text=True)
        assert "-dynamiclib" in darwin and "@rpath/libwebgpu_doe_c.dylib" in darwin
        assert "-lcompiler_probe" in darwin and "-exported_symbols_list," in darwin
        assert "--version-script" not in darwin and "-Bsymbolic" not in darwin and "-l:" not in darwin
        linux = subprocess.check_output(command + ["SHARED_PLATFORM=Linux", "all"], text=True)
        assert "-shared -Wl,-z,defs -Wl,-Bsymbolic-functions" in linux
        assert "libwebgpu_doe_c.so" in linux and "-l:libcompiler_probe.dylib" in linux
        assert "-dynamiclib" not in linux and "-exported_symbols_list" not in linux
        exports_file = Path(directory) / "native.exports"
        subprocess.run(["make", "--no-print-directory", "-s", "-C", str(owner),
                        f"BUILD={directory}", "SHARED_PLATFORM=Darwin", str(exports_file)], check=True)
        expected = {"_" + name for name in (owner / "EXPORTED_SYMBOLS.txt").read_text().splitlines()}
        assert set(exports_file.read_text().splitlines()) == expected
    print("PASS Linux/Darwin native link recipe controls, exact Mach-O export projection and nm normalization; no Apple compilation or execution")


if __name__ == "__main__":
    main()
