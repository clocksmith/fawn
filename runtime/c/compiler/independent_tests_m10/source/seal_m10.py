"""Verify the preserved concrete source commitment before candidate actions."""
from __future__ import annotations
import hashlib
from pathlib import Path

def verify_seal() -> dict[str,str]:
    root=Path(__file__).resolve().parent.parent;entries={}
    for line in (root/'CONCRETE_SOURCE.sha256').read_text().splitlines():
        digest,name=line.split('  ',1);path=(root/name).resolve()
        if root not in path.parents:raise ValueError('unsafe manifest path')
        if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('concrete evaluator seal differs: '+name)
        entries[name]=digest
    if not entries:raise ValueError('empty evaluator seal')
    return entries
