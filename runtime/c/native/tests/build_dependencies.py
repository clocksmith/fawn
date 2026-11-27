#!/usr/bin/env python3
"""Exercise native header dependencies and compiler flags using real object builds."""
from pathlib import Path
import shlex
import subprocess
import tempfile


def main() -> None:
    owner = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="doe-native-dependencies-") as directory:
        build = Path(directory)
        texture = build / "native/d3d12_texture_desc.o"
        futures = build / "owners/futures/src/futures.o"
        backend = build / "native/backend.o"
        objects = (texture, futures, backend)

        def run(*options: str) -> set[Path]:
            before = {path: path.stat().st_mtime_ns if path.exists() else None for path in objects}
            subprocess.run(["make", "-s", "-C", str(owner), f"BUILD={build}", *options, *objects],
                           check=True, timeout=120)
            return {path for path in objects if path.stat().st_mtime_ns != before[path]}

        def check_header(header: str, expected: set[Path]) -> None:
            # Compilers can spell the same relative include via different paths.
            # -W uses make's exact spelling and leaves shared checkout mtimes intact.
            names = set()
            for path in objects:
                content = path.with_suffix(".d").read_text().replace("\\\n", " ")
                for name in shlex.split(content.splitlines()[0].split(":", 1)[1]):
                    if (owner / name).resolve() == (owner / header).resolve():
                        names.add(name)
            assert names, header
            rebuilt = run(*(item for name in sorted(names) for item in ("-W", name)))
            assert rebuilt == expected, (header, rebuilt, expected)

        assert run() == set(objects)
        assert run() == set()
        check_header("../backends/d3d12/include/doe_d3d12_formats.inc", {texture, backend})
        check_header("../futures/src/internal.h", {futures})
        check_header("../../vendor/webgpu-headers/webgpu.h", {texture, backend})
        assert run("VK_PROFILE_CLEAR=1u") == set(objects)
        assert run("VK_PROFILE_CLEAR=1u") == set()
        assert run() == set(objects)
    print("PASS real native objects rebuild on used headers, include tables and changed flags")


if __name__ == "__main__":
    main()
