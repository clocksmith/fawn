"""Original full98 consumer; preserve fresh/prebuilt/skipped stage separation."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
import corpus_m8 as old
import witnesses_m9
from semantic_m9 import execute_vectors
from compute_oracle_m9 import Module
from retained_m9 import LegacyAdapter
from semantic_m8 import sha

ORIGINAL_INVENTORY=old.frozen_inventory
ORIGINAL_CASES=old.instruction_cases


def inventory(repo: Path) -> tuple:
    rows,classes=ORIGINAL_INVENTORY(repo)
    for name,source,expected in witnesses_m9.positives(repo):
        path=expected['source_path']
        if path not in classes:raise ValueError('new witness is outside original98')
        classes[path]=dict(reason='Frozen unchanged M9 function/math witness',capability='M9')
    return rows,classes


def instruction_cases(repo: Path) -> dict:
    known=ORIGINAL_CASES(repo)
    for name,source,expected in witnesses_m9.positives(repo):known[sha(source)]=('compute_m9',name,expected)
    return known


def main() -> int:
    old.frozen_inventory=inventory;old.instruction_cases=instruction_cases
    old.Module=Module;old.LegacyAdapter=LegacyAdapter;old.execute_vectors=execute_vectors
    return old.main()


if __name__=='__main__':raise SystemExit(main())
