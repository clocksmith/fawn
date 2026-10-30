# SPDX-License-Identifier: MIT
"""One selected-SDK FXC packaging owner shared by native Doe and Dawn builds."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import shutil
import subprocess
from typing import Callable


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture_command(command: list[str], directory: Path) -> str:
    return subprocess.run(command, cwd=directory, capture_output=True, text=True,
                          check=True, timeout=30).stdout.strip()


def stage_sdk_fxc(products: Path, sdk_root: Path, sdk_version: str,
                  capture: Callable[[list[str], Path], str] | None = None) -> dict[str, str]:
    """Package FXC from the explicitly selected x64 Windows SDK only."""
    if capture is None:
        capture = capture_command
    version = sdk_version.rstrip('\\/')
    if not sdk_root.is_absolute() or not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
        raise ValueError('explicit selected Windows SDK root and version required for FXC')
    source = sdk_root / 'bin' / version / 'x64/d3dcompiler_47.dll'
    if not source.is_file():
        raise ValueError(f'selected Windows SDK FXC runtime missing: {source}')
    destination = products / source.name
    if destination.exists():
        raise ValueError('FXC destination already exists; preserve its producer separately')
    headers = capture(['dumpbin', '/nologo', '/headers', str(source)], products)
    exports = capture(['dumpbin', '/nologo', '/exports', str(source)], products)
    if not re.search(r'^\s*8664 machine \(x64\)\s*$', headers, re.M):
        raise ValueError('selected Windows SDK FXC runtime must be x64')
    if any(not re.search(r'\b' + name + r'\s*$', exports, re.M)
           for name in ['D3DCompile', 'D3DDisassemble']):
        raise ValueError('selected Windows SDK FXC runtime lacks required compiler exports')
    shutil.copy2(source, destination)
    source_hash = digest(source)
    if digest(destination) != source_hash:
        raise ValueError('staged Windows SDK FXC runtime hash mismatch')
    return {'source_path': str(source), 'windows_sdk_version': version, 'sha256': source_hash,
            'architecture': 'x64',
            'headers_sha256': hashlib.sha256(headers.encode()).hexdigest(),
            'exports_sha256': hashlib.sha256(exports.encode()).hexdigest()}

