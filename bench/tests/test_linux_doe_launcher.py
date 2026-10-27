"""Real ELF launch, relocation and inherited-descriptor controls; no GPU claim."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "browser/chromium/scripts/patch-linux-browser-doe.py"
SPEC = importlib.util.spec_from_file_location("linux_launcher_installer", INSTALLER)
assert SPEC and SPEC.loader
OWNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OWNER)


@unittest.skipUnless(sys.platform == "linux", "requires actual Linux ELF/exec and /proc")
class LinuxLauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="fawn-launch-control-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / "staged browser"
        self.package.mkdir()
        self.stage = self.root / "C stage"
        (self.stage / "lib").mkdir(parents=True)
        (self.stage / "licenses").mkdir()
        (self.stage / "licenses/Doe.txt").write_text("test fixture license")
        self.compile(self.package / "chrome", r'''
            #include <stdio.h>
            #include <stdlib.h>
            #include <unistd.h>
            int main(int argc, char **argv) {
                printf("%ld\n", (long)getpid());
                for (int i = 0; i < argc; i++) printf("%s\n", argv[i]);
                const char *fd = getenv("FAWN_TEST_PIPE_FD");
                if (fd && write(atoi(fd), "X", 1) != 1) return 9;
                return 7;
            }''')
        artifacts = {}
        for name in ("libwebgpu_doe_c.so", "libdoe_compiler.so"):
            path = self.stage / "lib" / name
            self.compile(path, "int fixture(void) { return 0; }", shared=True)
            artifacts["lib/" + name] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.stage / "MANIFEST.json").write_text(json.dumps({"artifacts": artifacts,
            "source_commit": "a" * 40, "source_tree": "b" * 40}))

    def compile(self, output: Path, source: str, shared: bool = False) -> None:
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                        *(["-shared", "-fPIC"] if shared else []), "-o", str(output)],
                       input=source, text=True, check=True, capture_output=True)

    def install(self) -> dict:
        return OWNER.install(self.package, self.stage, "cc", False)

    def test_relocated_default_preserves_pid_descriptors_and_arguments(self) -> None:
        receipt = self.install()
        self.assertIn("chrome-wrapper", receipt["artifacts"])
        relocated = self.root / "moved ü browser"
        shutil.move(self.package, relocated)
        read_fd, write_fd = os.pipe()
        try:
            process = subprocess.Popen([str(relocated / "chrome-wrapper"), "--remote-debugging-pipe", "https://example.test/a b"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, pass_fds=(write_fd,),
                env={**os.environ, "FAWN_TEST_PIPE_FD": str(write_fd)})
            os.close(write_fd)
            write_fd = -1
            stdout, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 7, stderr)
            lines = stdout.splitlines()
            self.assertEqual(int(lines[0]), process.pid, "launcher must exec, preserving process ownership")
            self.assertEqual(lines[1], str(relocated / "chrome"))
            self.assertEqual(lines[2], "--use-webgpu-runtime=doe")
            self.assertEqual(lines[3], "--doe-webgpu-library-path=" + str(relocated / "lib/doe/libwebgpu_doe_c.so"))
            self.assertTrue(lines[4].startswith("--user-data-dir=/"))
            self.assertTrue(lines[4].endswith("/fawn/doe-c"))
            self.assertEqual(lines[5], "--gpu-sandbox-start-early")
            self.assertEqual(lines[6:], ["--remote-debugging-pipe", "https://example.test/a b"])
            self.assertEqual(os.read(read_fd, 1), b"X")
            self.assertNotIn("--no-sandbox", lines)
        finally:
            os.close(read_fd)
            if write_fd != -1:
                os.close(write_fd)

    def test_missing_c_fails_and_only_explicit_dawn_rolls_back(self) -> None:
        self.install()
        (self.package / "lib/doe/libwebgpu_doe_c.so").unlink()
        failed = subprocess.run([str(self.package / "chrome-wrapper")], capture_output=True, text=True)
        self.assertEqual(failed.returncode, 1)
        self.assertIn("no fallback", failed.stderr)
        self.assertEqual(failed.stdout, "")
        dawn = subprocess.run([str(self.package / "chrome-wrapper"), "--use-webgpu-runtime=dawn"], capture_output=True, text=True)
        self.assertEqual(dawn.returncode, 7, dawn.stderr)
        lines = dawn.stdout.splitlines()
        self.assertEqual(lines[2], "--use-webgpu-runtime=dawn")
        self.assertTrue(lines[3].endswith("/fawn/dawn"))
        self.assertNotIn("--gpu-sandbox-start-early", lines)

    def test_ambiguous_or_foreign_library_selection_is_rejected(self) -> None:
        self.install()
        for arguments in (["--use-webgpu-runtime=auto"], ["--use-webgpu-runtime=dawn", "--use-webgpu-runtime=doe"],
                          ["--doe-webgpu-library-path=/elsewhere.so"], ["--user-data-dir=relative"],
                          ["--user-data-dir=/one", "--user-data-dir=/two"],
                          ["-doe-webgpu-library-path=/elsewhere.so"], ["-use-webgpu-runtime=auto"],
                          [" \t--use-webgpu-runtime=doe", "-use-webgpu-runtime=dawn"],
                          [" \t-user-data-dir=relative \r"]):
            failed = subprocess.run([str(self.package / "chrome-wrapper"), *arguments], capture_output=True, text=True)
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(failed.stdout, "")

    def test_pinned_chromium_switch_forms_preserve_forced_selection(self) -> None:
        self.install()
        for spelling in ["-use-webgpu-runtime=dawn", " \t--use-webgpu-runtime=dawn\r\n"]:
            process = subprocess.run([str(self.package / "chrome-wrapper"), spelling], capture_output=True, text=True)
            self.assertEqual(process.returncode, 7, process.stderr)
            self.assertEqual(process.stdout.splitlines()[2], "--use-webgpu-runtime=dawn")
            self.assertTrue(process.stdout.splitlines()[3].endswith("/fawn/dawn"))
        for terminator in ["--", " \t--\r"]:
            process = subprocess.run([str(self.package / "chrome-wrapper"), terminator, "-use-webgpu-runtime=dawn"], capture_output=True, text=True)
            self.assertEqual(process.returncode, 7, process.stderr)
            self.assertEqual(process.stdout.splitlines()[2], "--use-webgpu-runtime=doe")

    def test_explicit_absolute_profile_is_preserved(self) -> None:
        self.install()
        profile = "--user-data-dir=" + str(self.root / "explicit ü profile")
        process = subprocess.run([str(self.package / "chrome-wrapper"), profile], capture_output=True, text=True)
        self.assertEqual(process.returncode, 7, process.stderr)
        self.assertEqual(process.stdout.splitlines()[4:], ["--gpu-sandbox-start-early", profile])

    def test_external_sandbox_ordering_is_canonical_before_user_arguments(self) -> None:
        self.install()
        for arguments in [[], ["--use-webgpu-runtime=doe"],
                          ["-gpu-sandbox-start-early"],
                          [" \t--gpu-sandbox-start-early=yes\r"]]:
            process = subprocess.run([str(self.package / "chrome-wrapper"), *arguments,
                                      "--remote-debugging-pipe"], capture_output=True, text=True)
            self.assertEqual(process.returncode, 7, process.stderr)
            lines = process.stdout.splitlines()
            self.assertEqual(lines.count("--gpu-sandbox-start-early"), 1)
            self.assertLess(lines.index("--gpu-sandbox-start-early"), lines.index("--remote-debugging-pipe"))
            self.assertFalse(any(value in lines for value in ["--no-sandbox", "--disable-gpu-sandbox",
                                                            "--enable-unsafe-swiftshader"]))

    def test_active_build_or_changed_c_stage_is_not_modified(self) -> None:
        (self.package / "build.ninja").write_text("active build")
        with self.assertRaisesRegex(ValueError, "active build"):
            self.install()
        (self.package / "build.ninja").unlink()
        with (self.stage / "lib/libdoe_compiler.so").open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.install()
        self.assertFalse((self.package / "lib/doe").exists())
        self.assertFalse((self.package / "chrome-wrapper").exists())


if __name__ == "__main__":
    unittest.main()
