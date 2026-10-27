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
if name == 'autoninja' and os.environ.get('FAWN_TEST_MAC_APP'):
    binary = pathlib.Path(sys.argv[2]) / 'Chromium.app/Contents/MacOS/Chromium'
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text('#!/bin/sh\\nexit 0\\n')
    binary.chmod(0o755)
if name == 'patch-chromium-app-doe.sh' and os.environ.get('FAWN_TEST_MAC_APP'):
    sys.exit(19)
"""


class ChromiumReleaseBuildOutputTests(unittest.TestCase):
    def _run_build(self, custom_output: bool, local_override: bool = False,
                   packaging_fails: bool = False) -> None:
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
                "patch-chromium-app-doe.sh",
            ):
                tool = scripts / name
                tool.write_text(FAKE_TOOL, encoding="utf-8")
                tool.chmod(0o755)
            (scripts / "uname").write_text(
                "#!/bin/sh\necho " + ("Darwin" if packaging_fails else "Linux") + "\n", encoding="utf-8",
            )
            (scripts / "uname").chmod(0o755)
            log = lane / "commands.jsonl"
            env = {**os.environ, "PATH": f"{scripts}:{os.environ['PATH']}",
                   "FAWN_TEST_COMMANDS": str(log)}
            for key in (
                "FAWN_CHROMIUM_RELEASE_GN_ARGS", "FAWN_CHROMIUM_LOCAL_JOBS",
                "FAWN_CHROMIUM_RELEASE_LOCAL_OUT",
            ):
                env.pop(key, None)
            if local_override:
                env["FAWN_CHROMIUM_RELEASE_LOCAL_OUT"] = str(lane / "explicit copies")
            env.pop("FAWN_TEST_MAC_APP", None)
            if packaging_fails:
                env["FAWN_TEST_MAC_APP"] = "1"
                env["FAWN_CHROMIUM_HOST_APP_PATH"] = str(lane / "private-host/Fawn.app")
            command = ["bash", str(scripts / BUILD_SCRIPT.name),
                       "--release-profile", "local"]
            output = "out/mac arm64" if custom_output else "out/fawn_release"
            if custom_output:
                command += ["--out-dir", output]
            command += ["-j", "2"]
            result = subprocess.run(command, env=env, capture_output=True)
            calls = [json.loads(line) for line in log.read_text(
                encoding="utf-8").splitlines()]
            if packaging_fails:
                self.assertEqual(result.returncode, 19, result.stderr)
                self.assertTrue(any(call[0] == "patch-chromium-app-doe.sh" for call in calls))
                self.assertFalse(any(call[0] == "sync-release-artifacts-local.sh" for call in calls))
                self.assertFalse((lane / "private-host/Fawn.app").exists())
                return
            self.assertEqual(result.returncode, 0, result.stderr)
            gn = next(call for call in calls if call[0] == "gn")
            self.assertEqual(gn[1:3], ["gen", output])
            self.assertIn("is_official_build=false", gn[3])
            self.assertIn(["autoninja", "-C", output, "chrome", "-j", "2"],
                          calls)
            sync = next(call for call in calls if call[0] == "sync-release-artifacts-local.sh")
            self.assertEqual(sync[1], output)
            if local_override or custom_output:
                expected = lane / "explicit copies" if local_override else lane / "src" / (output + "_local")
                self.assertEqual(Path(sync[2]).resolve(), expected.resolve())
                self.assertNotEqual(Path(sync[2]).resolve(), (lane / "out/fawn_release_local").resolve())
            else:
                self.assertEqual(sync, ["sync-release-artifacts-local.sh", output])
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

    def test_explicit_copy_destination_remains_authoritative(self) -> None:
        self._run_build(custom_output=True, local_override=True)

    def test_mac_packaging_failure_stops_before_copy_or_host_install(self) -> None:
        self._run_build(custom_output=True, packaging_fails=True)

    @unittest.skipUnless(shutil.which("rsync"), "bundle copy needs rsync")
    def test_direct_sync_returns_packaging_failure(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fawn copy failure ") as temporary:
            lane = Path(temporary)
            scripts = lane / "scripts"
            scripts.mkdir()
            sync = scripts / "sync-release-artifacts-local.sh"
            shutil.copy2(BUILD_SCRIPT.parent / sync.name, sync)
            patch = scripts / "patch-chromium-app-doe.sh"
            patch.write_text(FAKE_TOOL)
            patch.chmod(0o755)
            uname = scripts / "uname"
            uname.write_text("#!/bin/sh\necho Darwin\n")
            uname.chmod(0o755)
            source = lane / "built"
            binary = source / "Chromium.app/Contents/MacOS/Chromium"
            binary.parent.mkdir(parents=True)
            binary.write_text("owned CPU fixture")
            env = {**os.environ, "PATH": f"{scripts}:{os.environ['PATH']}",
                   "FAWN_TEST_COMMANDS": str(lane / "calls.jsonl"), "FAWN_TEST_MAC_APP": "1"}
            result = subprocess.run(["bash", str(sync), str(source), str(lane / "copy")],
                                    env=env, capture_output=True)
            self.assertEqual(result.returncode, 19, result.stderr)
            self.assertNotIn(b"synced release artifacts", result.stdout)


if __name__ == "__main__":
    unittest.main()
