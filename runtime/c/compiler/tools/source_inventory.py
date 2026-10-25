# SPDX-License-Identifier: MIT
"""Read the canonical compiler inventories without interpreting arbitrary Make."""
from pathlib import Path
import re


def target_names(text: str, variable: str) -> list[str]:
    names: list[str] = []
    initialized = False
    pending = ""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if line.endswith("\\"):
            pending += line[:-1] + " "
            continue
        line = pending + line
        pending = ""
        if not line.strip():
            continue
        assignment = re.fullmatch(r"\s*" + re.escape(variable) + r"\s*(\+=|=)\s*(.*?)\s*", line)
        if assignment is None:
            raise ValueError(f"unsupported {variable} source inventory statement: {line}")
        operator, value = assignment.groups()
        if (operator == "=" and initialized) or (operator == "+=" and not initialized):
            raise ValueError(f"invalid assignment order for {variable}")
        initialized = True
        for name in value.split():
            if not re.fullmatch(r"[a-z0-9_]+\.c", name):
                raise ValueError(f"invalid target source name: {name}")
            if name in names:
                raise ValueError(f"duplicate target source: {name}")
            names.append(name)
    if pending or not initialized or not names:
        raise ValueError(f"incomplete {variable} source inventory")
    return names


def compiler_sources(root: Path) -> list[Path]:
    paths = root.joinpath("src/sources.txt").read_text().split()
    for target in ("metal", "hlsl"):
        names = target_names(root.joinpath("targets", target, "sources.mk").read_text(),
                             target.upper() + "_SOURCE_NAMES")
        paths.extend(f"targets/{target}/{name}" for name in names)
    if len(paths) != len(set(paths)):
        raise ValueError("duplicate compiler source")
    for path in paths:
        if not re.fullmatch(r"(src|targets/(metal|hlsl))/[a-z0-9_]+\.c", path):
            raise ValueError(f"invalid compiler source path: {path}")
        if not root.joinpath(path).is_file():
            raise FileNotFoundError(path)
    return [root / path for path in paths]
