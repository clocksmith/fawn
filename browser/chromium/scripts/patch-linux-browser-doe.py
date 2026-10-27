#!/usr/bin/env python3
"""Install the C-default entry point into a staged Linux browser package."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def header(path: Path) -> bytes:
    with path.open("rb") as stream:
        return stream.read(20)


def install(package: Path, stage: Path, cc: str, replace_wrapper: bool) -> dict:
    if platform.system() != "Linux":
        raise ValueError("Linux installation must compile and run on its target host")
    package, stage = package.resolve(strict=True), stage.resolve(strict=True)
    if (package / "build.ninja").exists() or (package / "args.gn").exists():
        raise ValueError("copy a complete staged package first; do not modify an active build output")
    browser = package / "chrome"
    wrapper = package / "chrome-wrapper"
    private = package / "lib/doe"
    if private.exists() or (wrapper.exists() and not replace_wrapper):
        raise FileExistsError("private closure already exists or wrapper replacement was not explicit")
    if not browser.is_file() or not os.access(browser, os.X_OK):
        raise ValueError("staged package must contain executable chrome")
    image_header = header(browser)
    if len(image_header) != 20 or image_header[:6] != b"\x7fELF\x02\x01" or image_header[18:20] not in (b"\x3e\x00", b"\xb7\x00"):
        raise ValueError("chrome must be an x64 or arm64 little-endian ELF image")
    manifest_path = stage / "MANIFEST.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    libraries = ["libwebgpu_doe_c.so", "libdoe_compiler.so"]
    for name in libraries:
        path = stage / "lib" / name
        if manifest.get("artifacts", {}).get("lib/" + name) != digest(path):
            raise ValueError(f"C stage manifest does not bind {name}")
        if header(path)[:6] != image_header[:6] or header(path)[18:20] != image_header[18:20]:
            raise ValueError(f"C stage architecture differs from chrome: {name}")
    source = Path(__file__).with_name("linux-doe-launcher.c")
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", str(source)]
    with tempfile.TemporaryDirectory(prefix="fawn-launcher-", dir=package.parent) as temporary:
        work = Path(temporary)
        executable = work / "chrome-wrapper"
        subprocess.run([*command, "-o", str(executable)], check=True)
        if header(executable)[:6] != image_header[:6] or header(executable)[18:20] != image_header[18:20]:
            raise ValueError("launcher compiler target differs from the staged browser")
        closure = work / "doe"
        closure.mkdir()
        for name in libraries:
            shutil.copy2(stage / "lib" / name, closure / name)
            if digest(closure / name) != manifest["artifacts"]["lib/" + name]:
                raise ValueError(f"C stage changed during copy: {name}")
        shutil.copy2(manifest_path, closure / "doe-c-manifest.json")
        shutil.copytree(stage / "licenses", closure / "licenses")
        if wrapper.exists():
            shutil.copy2(wrapper, closure / "chrome-wrapper.before-doe")
        receipt = {"scope": "C-default launcher installation, not browser/release qualification",
                   "sourceCommit": manifest["source_commit"], "sourceTree": manifest["source_tree"],
                   "manifestSha256": digest(manifest_path), "launcherSourceSha256": digest(source),
                   "installerSha256": digest(Path(__file__)), "compilerCommand": command,
                   "artifacts": {"chrome": digest(browser), "chrome-wrapper": digest(executable),
                                 **{"lib/doe/" + name: digest(closure / name) for name in libraries}}}
        (closure / "launcher-install.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        private.parent.mkdir(exist_ok=True)
        shutil.move(str(closure), private)
        try:
            os.replace(executable, wrapper)
        except BaseException:
            shutil.rmtree(private)
            raise
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--c-stage", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--replace-wrapper", action="store_true")
    args = parser.parse_args()
    print(json.dumps(install(args.package_dir, args.c_stage, args.cc, args.replace_wrapper), sort_keys=True))


if __name__ == "__main__":
    main()
