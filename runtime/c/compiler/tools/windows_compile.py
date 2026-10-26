# SPDX-License-Identifier: MIT
"""One strict compiler invocation shared by the Windows core and host builders."""
from __future__ import annotations


def strict_command(compiler: str) -> list[str]:
    return [compiler, "--target=x86_64-pc-windows-msvc", "/nologo", "/MD",
            "/O2", "/WX", "/D_CRT_SECURE_NO_WARNINGS", "/clang:-std=c11",
            "/clang:-fno-fast-math", "/clang:-ffp-contract=off",
            "/clang:-frounding-math", "/clang:-Wall", "/clang:-Wextra",
            "/clang:-Wpedantic", "/clang:-Wconversion", "/clang:-Wshadow",
            "/clang:-Wstrict-prototypes", "/clang:-Wmissing-prototypes",
            "/clang:-Wswitch-enum"]
