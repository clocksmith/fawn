"""Exercise output-directory isolation through the release build wrapper."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_SCRIPT = (
    REPO_ROOT / "browser/chromium/scripts/build-release-external.sh"
)
FAKE_TOOL = """#!/usr/bin/env python3
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['FAWN_TEST_COMMANDS'], 'a', encoding='utf-8') as log:
    log.write(json.dumps([name, *sys.argv[1:]]) + '\\n')
if name == 'gn':
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    (out / 'args.gn').write_text(sys.argv[3] + '\\n', encoding='utf-8')
"""


class ChromiumReleaseBuildOutputTests(unittest.TestCase):
    def _run_build(self, custom_output: bool) -> None:
        with tempfile.TemporaryDirectory(prefix="fawn build ") as temporary:
            lane = Path(temporary)
            scripts = lane / "scripts"
            scripts.mkdir()
            (lane / "src").mkdir()
            shutil.copy2(BUILD_SCRIPT, scripts / BUILD_SCRIPT.name)
            (scripts / "env.sh").write_text("", encoding="utf-8")
            for name in (
                "gn", "autoninja", "apply-runtime-patches.sh",
                "sync-release-artifacts-local.sh",
            ):
                tool = scripts / name
                tool.write_text(FAKE_TOOL, encoding="utf-8")
                tool.chmod(0o755)
            (scripts / "uname").write_text(
                "#!/bin/sh\necho Linux\n", encoding="utf-8",
            )
            (scripts / "uname").chmod(0o755)
            log = lane / "commands.jsonl"
            env = {**os.environ, "PATH": f"{scripts}:{os.environ['PATH']}",
                   "FAWN_TEST_COMMANDS": str(log)}
            for key in (
                "FAWN_CHROMIUM_RELEASE_GN_ARGS", "FAWN_CHROMIUM_LOCAL_JOBS",
            ):
                env.pop(key, None)
            command = ["bash", str(scripts / BUILD_SCRIPT.name),
                       "--release-profile", "local"]
            output = "out/mac arm64" if custom_output else "out/fawn_release"
            if custom_output:
                command += ["--out-dir", output]
            command += ["-j", "2"]
            subprocess.run(command, env=env, check=True, capture_output=True)
            calls = [json.loads(line) for line in log.read_text(
                encoding="utf-8").splitlines()]
            gn = next(call for call in calls if call[0] == "gn")
            self.assertEqual(gn[1:3], ["gen", output])
            self.assertIn("is_official_build=false", gn[3])
            self.assertIn(["autoninja", "-C", output, "chrome", "-j", "2"],
                          calls)
            self.assertIn(["sync-release-artifacts-local.sh", output], calls)
            built = lane / "src" / output
            metadata = json.loads((built / "fawn-release-build.json").read_text(
                encoding="utf-8"))
            self.assertEqual(metadata["releaseProfile"], "local")
            self.assertEqual(metadata["argsSha256"], hashlib.sha256(
                (built / "args.gn").read_bytes()).hexdigest())
            if custom_output:
                self.assertFalse((lane / "src/out/fawn_release").exists())

    def test_default_output_is_preserved(self) -> None:
        self._run_build(custom_output=False)

    def test_custom_output_is_used_for_every_build_stage(self) -> None:
        self._run_build(custom_output=True)


if __name__ == "__main__":
    unittest.main()
