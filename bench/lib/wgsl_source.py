"""Canonical newline normalization and source identity for WGSL receipts."""

from __future__ import annotations

import hashlib
from pathlib import Path


def normalize_source(text: str) -> str:
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    if not normalized.endswith("\n"):
        normalized += "\n"
    return normalized


def normalized_sha256(path: Path) -> str:
    return hashlib.sha256(
        normalize_source(path.read_text(encoding="utf-8")).encode("utf-8")
    ).hexdigest()
