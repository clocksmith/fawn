#!/usr/bin/env python3
"""Run the original manifest-bound build with only its wall timeout disabled."""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) < 4:
        raise ValueError("Expected original builder path, its SHA256, then build arguments")
    original = Path(sys.argv[1]).resolve()
    expected = sys.argv[2]
    if hashlib.sha256(original.read_bytes()).hexdigest() != expected:
        raise ValueError("Original build front-door identity mismatch")
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("m11_unchanged_build", original)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original_receipt = module.Receipt

    class SupervisedReceipt(original_receipt):
        def run(self, label, command, required=True, timeout=None, env=None):
            # Only scheduling changes. Original commands, flags, assertions,
            # output binding, diagnostics and first-failure retention remain.
            return super().run(label, command, required=required,
                               timeout=None, env=env)

    module.Receipt = SupervisedReceipt
    sys.argv = [str(original), *sys.argv[3:]]
    return module.main()


if __name__ == "__main__":
    raise SystemExit(main())
