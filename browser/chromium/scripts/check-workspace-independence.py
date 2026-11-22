#!/usr/bin/env python3
"""Reject direct workspace dependencies in the staged Fawn/Doe source tree."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = "browser/chromium/scripts/check-workspace-independence.py"
PRODUCTION = (
    "browser/chromium", "runtime", "packages",
    ":(exclude,glob)**/test/**", ":(exclude,glob)**/tests/**",
    ":(exclude,glob)**/artifacts/**", ":(exclude)" + SCRIPT,
)
WORKSPACE = (
    r"@doe/blitzen|@gamma/workspace|(browser|gamma)/workspace([^[:alnum:]_-]|$)"
    r"""|(\.\./)+workspace(/|["'])|fawn-workspace-service\.json"""
)
SUFFIXES = {
    ".cc", ".cpp", ".h", ".c", ".zig", ".py", ".sh", ".js", ".mjs", ".cjs",
    ".ts", ".json", ".jsonc", ".gn", ".gni", ".toml", ".patch", ".yml", ".yaml",
}


def direct_dependencies(root: Path) -> list[str]:
    # Search the index so sparse checkouts cannot silently omit production files.
    result = subprocess.run(
        ["git", "grep", "--cached", "-I", "-n", "-E", WORKSPACE, "--", *PRODUCTION],
        cwd=root, text=True, stdout=subprocess.PIPE, check=False,
    )
    if result.returncode not in (0, 1):
        result.check_returncode()
    return [
        line.split(":", 2)[0] + ":" + line.split(":", 2)[1]
        for line in result.stdout.splitlines()
        if Path(line.split(":", 1)[0]).suffix in SUFFIXES
    ]


def main() -> int:
    violations = direct_dependencies(ROOT)
    if violations:
        print("Workspace dependency in staged Fawn/Doe production files:")
        print("\n".join(violations))
        return 1
    print("Fawn/Doe staged direct workspace dependency check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
