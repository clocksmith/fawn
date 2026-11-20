#!/usr/bin/env python3
"""Compile and execute the exact-plane CPU test against a pinned Linux Chromium component build."""

import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

TARGET = "obj/third_party/blink/renderer/modules/webcodecs/webcodecs/video_frame.o"


def check(build, source, overlay):
    build = build.resolve()
    source = source.resolve()
    test = Path(__file__).resolve().with_name("restricted-i420-test.cc")
    compiler = source / "third_party/llvm-build/Release+Asserts/bin/clang++"
    command = subprocess.check_output(["ninja", "-t", "commands", "-s", TARGET], cwd=build, text=True)
    with tempfile.TemporaryDirectory(prefix="blitzen-i420-") as tmp:
        directory = Path(tmp)
        objects = []
        for path in (test, source / "base/test/test_discardable_memory_allocator.cc"):
            args = shlex.split(command.strip())
            obj = directory / (path.stem + ".o")
            args[args.index("-c") + 1] = str(path)
            args[args.index("-o") + 1] = str(obj)
            args[args.index("-MF") + 1] = str(obj.with_suffix(".d"))
            if overlay:
                args.insert(1, "-I" + str(overlay.resolve()))
            subprocess.run(args, cwd=build, check=True)
            objects.append(str(obj))
        dependencies = ["media", "base", "cc_paint", "skia", "ui_gfx_color_space", "ui_gfx_geometry", "ui_gfx", "c++"]
        allocator = sorted(build.glob("libbase_allocator_partition*.so"))
        executable = directory / "check-native-planes"
        subprocess.run([str(compiler), "-fuse-ld=lld", *objects, "-L" + str(build),
                        "-Wl,-rpath," + str(build), "-Wl,-rpath-link," + str(build),
                        *["-l" + name for name in dependencies], *["-l:" + path.name for path in allocator],
                        "-o", str(executable)], cwd=directory, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-src", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="The pinned Linux component build's output directory")
    parser.add_argument("--overlay", type=Path, help="Read-only source overlay containing the applied optional patch")
    args = parser.parse_args()
    check(args.build, args.chromium_src, args.overlay)
