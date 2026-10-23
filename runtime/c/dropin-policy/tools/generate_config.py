#!/usr/bin/env python3
"""Admit embedded JSON into immutable ABI-1 data, without runtime parsing.

Syntax is scanned iteratively and numbers remain tokens. The two consumers have
separate duplicate-key policies; schemas are provenance inputs, not validators.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
from json.decoder import scanstring
import os
from pathlib import Path
import re
import sys
import tempfile

INPUT_LIMIT = 65536
OUTPUT_LIMIT = 16 * 1024 * 1024
PROFILE = 1
NUMBER = re.compile(r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?")
MODES = {
    "dawn_ownership": "DOE_DROPIN_MODE_DAWN",
    "mixed_ownership": "DOE_DROPIN_MODE_MIXED",
    "doe_metal_ownership": "DOE_DROPIN_MODE_METAL",
    "doe_vulkan_ownership": "DOE_DROPIN_MODE_VULKAN",
    "doe_d3d12_ownership": "DOE_DROPIN_MODE_D3D12",
}
OWNERS = {
    "dawn_delegate": "DOE_DROPIN_OWNER_DAWN",
    "doe_metal": "DOE_DROPIN_OWNER_METAL",
    "doe_vulkan": "DOE_DROPIN_OWNER_VULKAN",
    "doe_d3d12": "DOE_DROPIN_OWNER_D3D12",
    "shared": "DOE_DROPIN_OWNER_SHARED",
}
DEFAULT_MODE = "DOE_DROPIN_MODE_DAWN"
USE_DEFAULT = "DOE_DROPIN_MODE_USE_DEFAULT"


class JsonSyntaxError(ValueError):
    """The input was readable, but is not strict Unicode JSON."""


@dataclass(frozen=True)
class NumberToken:
    raw: str


@dataclass
class ObjectPairs:
    pairs: list[tuple[str, object]] = field(default_factory=list)


@dataclass
class Frame:
    value: ObjectPairs | list[object]
    state: str
    key: str = ""
    keys: set[str] = field(default_factory=set)


def parse_json(raw: bytes) -> tuple[object, bool]:
    """Return ordered JSON and whether any decoded object key is duplicated.

    A frame is pushed only for an actual input opening bracket. No conversion
    of numeric magnitudes or recursive traversal is performed.
    """
    try:
        source = raw.decode("utf-8", errors="strict")
    except UnicodeError as exc:
        raise JsonSyntaxError("invalid UTF-8") from exc
    size = len(source)
    cursor = 0
    frames: list[Frame] = []
    root: object = None
    has_root = False
    has_duplicates = False

    def string_at(start: int) -> tuple[str, int]:
        try:
            value, end = scanstring(source, start + 1, True)
            value.encode("utf-8", errors="strict")
        except (ValueError, UnicodeError) as exc:
            raise JsonSyntaxError("invalid JSON string or Unicode scalar") from exc
        return value, end

    while True:
        while cursor < size and source[cursor] in " \t\r\n":
            cursor += 1
        if not frames and has_root:
            if cursor != size:
                raise JsonSyntaxError("trailing bytes")
            return root, has_duplicates
        if cursor == size:
            raise JsonSyntaxError("missing value or closing delimiter")
        if frames:
            frame = frames[-1]
            is_object = isinstance(frame.value, ObjectPairs)
            closing = "}" if is_object else "]"
            char = source[cursor]
            if frame.state in ("first", "after") and char == closing:
                frames.pop()
                cursor += 1
                continue
            if frame.state == "after":
                if char != ",":
                    raise JsonSyntaxError("expected comma or closing delimiter")
                frame.state = "key" if is_object else "value"
                cursor += 1
                continue
            if is_object and frame.state in ("first", "key"):
                if char != '"':
                    raise JsonSyntaxError("expected object key")
                frame.key, cursor = string_at(cursor)
                if frame.key in frame.keys:
                    has_duplicates = True
                frame.keys.add(frame.key)
                frame.state = "colon"
                continue
            if frame.state == "colon":
                if char != ":":
                    raise JsonSyntaxError("expected colon")
                frame.state = "value"
                cursor += 1
                continue
        char = source[cursor]
        value: object
        if char == '"':
            value, cursor = string_at(cursor)
        elif char in "[{":
            value = ObjectPairs() if char == "{" else []
            cursor += 1
        elif source.startswith("true", cursor):
            value = True
            cursor += 4
        elif source.startswith("false", cursor):
            value = False
            cursor += 5
        elif source.startswith("null", cursor):
            value = None
            cursor += 4
        else:
            match = NUMBER.match(source, cursor)
            if match is None:
                raise JsonSyntaxError("invalid value")
            value = NumberToken(match.group())
            cursor = match.end()
        if frames:
            frame = frames[-1]
            if isinstance(frame.value, ObjectPairs):
                frame.value.pairs.append((frame.key, value))
            else:
                frame.value.append(value)
            frame.state = "after"
        else:
            root, has_root = value, True
        if isinstance(value, (ObjectPairs, list)):
            frames.append(Frame(value, "first"))


def ascii_token(value: object, choices: dict[str, str]) -> str | None:
    if not isinstance(value, str) or not value.isascii():
        return None
    return choices.get(value.lower())


@dataclass(frozen=True)
class Behavior:
    status: str = "DOE_DROPIN_OK"
    default_mode: str = DEFAULT_MODE
    strict: int = 1
    observe_environment: int = 0
    lane_modes: tuple[tuple[bytes, str], ...] = ()


def behavior_config(raw: bytes) -> Behavior:
    """Classify syntax and behavior; deferred fatal outcomes remain data."""
    try:
        root, duplicates = parse_json(raw)
    except JsonSyntaxError:
        return Behavior()
    if not isinstance(root, ObjectPairs) or duplicates:
        return Behavior()
    fields = dict(root.pairs)  # Uniqueness was established for every object.
    mode_value = fields.get("defaultMode")
    mode = ascii_token(mode_value, MODES)
    strict = fields.get("strictFallbackForbidden", True)
    if type(strict) is not bool:
        return Behavior()
    if strict and isinstance(mode_value, str) and mode is None:
        return Behavior(status="DOE_DROPIN_FATAL_DEFAULT_MODE")
    lanes = fields.get("laneModes")
    rows: list[tuple[bytes, str]] = []
    if isinstance(lanes, ObjectPairs):
        for key, value in lanes.pairs:
            lane_mode = ascii_token(value, MODES)
            if strict and isinstance(value, str) and lane_mode is None:
                return Behavior(status="DOE_DROPIN_FATAL_LANE_MODE")
            rows.append((key.encode("utf-8"), lane_mode or USE_DEFAULT))
    return Behavior(default_mode=mode or DEFAULT_MODE, strict=int(strict),
                    observe_environment=int(isinstance(lanes, ObjectPairs)),
                    lane_modes=tuple(rows))


def known_fields(value: object, names: tuple[str, ...]) -> dict[str, object] | None:
    if not isinstance(value, ObjectPairs):
        return None
    fields: dict[str, object] = {}
    for key, item in value.pairs:
        if key in names:
            if key in fields:
                return None
            fields[key] = item
    return fields if len(fields) == len(names) else None


def ownership_config(raw: bytes) -> tuple[tuple[bytes, str], ...]:
    """Admit profile-1 typed fields while ignoring unknown-field duplicates."""
    try:
        root, _duplicates = parse_json(raw)
    except JsonSyntaxError:
        return ()
    fields = known_fields(root, ("schemaVersion", "symbols"))
    if fields is None:
        return ()
    version = fields["schemaVersion"]
    if not isinstance(version, NumberToken) or version.raw not in ("2", "3"):
        return ()
    symbols = fields["symbols"]
    if not isinstance(symbols, list):
        return ()
    rows: list[tuple[bytes, str]] = []
    for item in symbols:
        entry = known_fields(item, ("symbol", "owner"))
        if entry is None or not isinstance(entry["symbol"], str):
            return ()
        owner = ascii_token(entry["owner"], OWNERS)
        if owner is None:
            return ()
        rows.append((entry["symbol"].encode("utf-8"), owner))
    return tuple(rows)


def read_input(path: Path) -> bytes:
    """Separate infrastructure/size errors from malformed configuration."""
    with path.open("rb") as stream:
        raw = stream.read(INPUT_LIMIT + 1)
    if len(raw) > INPUT_LIMIT:
        raise ValueError(f"{path}: exceeds {INPUT_LIMIT}-byte input limit")
    return raw


def atomic_write(path: Path, data: bytes) -> None:
    """Publish a complete file, preserving both old bytes and stable mtimes."""
    if path.is_file() and path.read_bytes() == data:
        return
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".",
                                         delete=False) as stream:
            temporary = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def byte_lines(data: bytes) -> list[str]:
    return ["    " + ", ".join(str(byte) for byte in data[offset:offset + 16]) + ","
            for offset in range(0, len(data), 16)]


def render_config(behavior_raw: bytes, ownership_raw: bytes) -> bytes:
    """Produce readable deterministic C with explicit byte lengths."""
    if max(len(behavior_raw), len(ownership_raw)) > INPUT_LIMIT:
        raise ValueError("input exceeds ABI-1 generator limit")
    behavior = behavior_config(behavior_raw)
    ownership = ownership_config(ownership_raw)
    lines = ['/* Generated by tools/generate_config.py; do not edit. */',
             '#include "doe_dropin_policy_private.h"', ""]
    for label, rows, c_type in (
            ("lane_modes", behavior.lane_modes, "DoeDropinLaneMode"),
            ("ownership", ownership, "DoeDropinOwnership")):
        # Each source byte can contribute at most one decoded byte or record.
        if len(rows) > INPUT_LIMIT or sum(len(row[0]) for row in rows) > INPUT_LIMIT:
            raise ValueError("generated record extent exceeds input bound")
        for index, (name, _code) in enumerate(rows):
            if name:
                lines.append(f"static const uint8_t {label}_{index}[] = {{")
                lines.extend(byte_lines(name))
                lines.extend(["};", ""])
        if rows:
            lines.append(f"static const {c_type} {label}[] = {{")
            for index, (name, code) in enumerate(rows):
                pointer = f"{label}_{index}" if name else "NULL"
                lines.append(f"    {{ {{ {pointer}, {len(name)} }}, {code} }},")
            lines.extend(["};", ""])
    lines.extend([
        "const DoeDropinEmbeddedConfig doe_dropin_policy_generated_config = {",
        "    .abi_version = DOE_DROPIN_POLICY_ABI_VERSION,",
        "    .struct_size = sizeof(DoeDropinEmbeddedConfig),",
        "    .admission_profile = DOE_DROPIN_POLICY_ADMISSION_PROFILE,",
        f"    .behavior_status = {behavior.status},",
        f"    .default_mode = {behavior.default_mode},",
        f"    .strict_no_fallback = {behavior.strict},",
        f"    .observe_environment = {behavior.observe_environment},",
        "    .reserved = 0,",
        "    .lane_modes = " + ("lane_modes," if behavior.lane_modes else "NULL,"),
        f"    .lane_mode_count = {len(behavior.lane_modes)},",
        "    .ownership = " + ("ownership," if ownership else "NULL,"),
        f"    .ownership_count = {len(ownership)},",
        "    .behavior_sha256 = {",
        *byte_lines(hashlib.sha256(behavior_raw).digest()),
        "    },", "    .ownership_sha256 = {",
        *byte_lines(hashlib.sha256(ownership_raw).digest()),
        "    },", "};", "",
    ])
    output = "\n".join(lines).encode("ascii")
    if len(output) > OUTPUT_LIMIT:
        raise ValueError("generated C exceeds checked output extent")
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--behavior", required=True, type=Path,
                        help="Actual embedded behavior JSON")
    parser.add_argument("--ownership", required=True, type=Path,
                        help="Actual embedded ownership JSON")
    parser.add_argument("--output", required=True, type=Path,
                        help="Atomically replaced generated C output")
    parser.add_argument("--profile", type=int, choices=[PROFILE], default=PROFILE,
                        help="Frozen admission profile (default: 1)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        output = render_config(read_input(args.behavior), read_input(args.ownership))
        atomic_write(args.output, output)
    except (OSError, ValueError, MemoryError) as exc:
        print(f"drop-in config generation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
