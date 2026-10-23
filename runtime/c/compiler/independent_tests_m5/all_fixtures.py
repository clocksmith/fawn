"""Frozen retained cases plus individually authorized M4 admission lifts."""
from __future__ import annotations
from pathlib import Path
import fixtures
import m4_fixtures

def admitted(root: Path) -> list:
    return fixtures.admitted(root)+m4_fixtures.admitted(root)

def rejected() -> list:
    return [case for case in fixtures.rejected() if case[0] not in m4_fixtures.LIFTS]+m4_fixtures.rejected()
