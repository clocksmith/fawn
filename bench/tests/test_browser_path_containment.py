"""Browser asset/output containment respects POSIX and Windows path boundaries."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "browser/chromium/scripts/browser-path-containment.mjs"


class BrowserPathContainmentTests(unittest.TestCase):
    def test_native_segments_drives_and_unc_roots(self) -> None:
        cases = {
            "posix": [
                ["/repo/assets/file.js", "/repo", True],
                ["/repo", "/repo", True],
                ["/repo/assets/../file.js", "/repo", True],
                ["/repo/..notes/file.js", "/repo", True],
                ["/repo/..\\secret", "/repo", True],
                ["/repository/file.js", "/repo", False],
                ["/repo/../secret", "/repo", False],
                ["/repo/..", "/repo", False],
                ["/REPO/file.js", "/repo", False],
                ["/tmp/file.js", "/", True],
            ],
            "win32": [
                [r"C:\repo\assets\file.js", r"C:\repo", True],
                [r"C:\repo", r"C:\repo", True],
                ["C:/repo/assets/file.js", r"C:\repo", True],
                [r"c:\REPO\assets\file.js", r"C:\repo", True],
                [r"C:\repo\assets\..\file.js", r"C:\repo", True],
                [r"C:\repo\..notes\file.js", r"C:\repo", True],
                [r"C:\repository\file.js", r"C:\repo", False],
                [r"C:\repo\..\secret", r"C:\repo", False],
                [r"C:\repo\..", r"C:\repo", False],
                [r"D:\repo\file.js", r"C:\repo", False],
                [r"\\host\share\repo\file.js", r"\\host\share\repo", True],
                [r"\\host\other\repo\file.js", r"\\host\share\repo", False],
                [r"\\other\share\repo\file.js", r"\\host\share\repo", False],
            ],
        }
        script = f"""
import assert from 'node:assert/strict';
import {{ posix, win32 }} from 'node:path';
import {{ pathWithin }} from {json.dumps(HELPER.as_uri())};
const cases = {json.dumps(cases)};
for (const [name, api] of Object.entries({{ posix, win32 }})) {{
  for (const [candidate, root, expected] of cases[name]) {{
    assert.equal(pathWithin(candidate, root, api), expected, `${{name}} ${{candidate}} from ${{root}}`);
  }}
}}
assert.equal(pathWithin('.', process.cwd()), true);
assert.equal(pathWithin('..', process.cwd()), false);
"""
        result = subprocess.run(["node", "--input-type=module", "-e", script],
                                cwd=ROOT, capture_output=True, text=True,
                                timeout=10, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
