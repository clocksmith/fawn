"""Synthetic macOS package inputs shared by packaging and provenance tests."""

from __future__ import annotations

import hashlib
import json
import plistlib
import struct
from pathlib import Path

from bench.tools import check_browser_release_package_inputs as package_inputs


def write_file(path: Path, payload: bytes, mode: int = 0o644) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    path.chmod(mode)
    return path


def macos_plist(*, display_name: str = "Fawn Doe", version: str = "0.0.0-sample") -> dict:
    return {
        "CFBundleName": display_name,
        "CFBundleDisplayName": display_name,
        "CFBundleIdentifier": "dev.doe.fawn-doe",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "CFBundleExecutable": "Chromium",
        "CFBundlePackageType": "APPL",
    }


def macho_payload(arch: str = "arm64") -> bytes:
    cpu_type = {
        "arm64": 0x0100000C,
        "x64": 0x01000007,
    }[arch]
    return struct.pack("<IiiIIIII", 0xFEEDFACF, cpu_type, 0, 2, 0, 0, 0, 0)


def write_canonical_args_gn(path: Path) -> Path:
    return write_file(
        path,
        b"\n".join(
            [
                b"is_debug = false",
                b"is_official_build = true",
                b"dcheck_always_on = false",
                b"chrome_pgo_phase = 0",
                b"symbol_level = 0",
                b"blink_symbol_level = 0",
                b"v8_symbol_level = 0",
                b"is_chrome_for_testing = false",
                b"is_chrome_for_testing_branded = false",
                b"is_chrome_branded = false",
                b"use_clang_modules = false",
                b"dawn_enable_webgpu_on_webgpu = true",
                b"",
            ]
        ),
    )


def write_macos_candidate_inputs(root: Path, *, version: str = "0.0.0-sample") -> dict[str, Path]:
    app_dir = root / "Fawn.app"
    write_canonical_args_gn(root / "args.gn")
    browser = app_dir / "Contents" / "MacOS" / "Chromium"
    write_file(browser, macho_payload(), 0o755)
    plist_path = app_dir / "Contents" / "Info.plist"
    plist_path.parent.mkdir(parents=True, exist_ok=True)
    with plist_path.open("wb") as handle:
        plistlib.dump(macos_plist(version=version), handle)
    return {
        "package_dir": app_dir,
        "browser": browser,
        "plist": plist_path,
        "doe_runtime": write_file(root / "libwebgpu_doe.so", macho_payload(), 0o755),
        "dawn_runtime": write_file(root / "libdawn_native.so", macho_payload(), 0o755),
        "compiler": write_file(root / "doe-zig-runtime", macho_payload(), 0o755),
    }


def write_macos_candidate_report(root: Path, product: dict, platform: dict) -> tuple[Path, dict]:
    root = root.resolve()
    inputs = write_macos_candidate_inputs(root, version=product["version"])
    report = package_inputs.build_report(
        package_dir=str(inputs["package_dir"]),
        package_root_name="Fawn.app",
        doe_runtime=str(inputs["doe_runtime"]),
        dawn_fallback_runtime=str(inputs["dawn_runtime"]),
        shader_compiler=str(inputs["compiler"]),
        product_version=product["version"],
        product_channel=product["channel"],
        platform_os=platform["os"],
        platform_arch=platform["arch"],
        root=root,
    )
    path = root / "browser-release-package-inputs.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return path, report


def write_clean_install_fixture(root: Path, archive: Path, manifest: Path, product: dict, platform: dict) -> Path:
    """Build a synthetic receipt with real hashes of the temporary test inputs."""
    def artifact(path: Path, kind: str) -> dict:
        return {"path": str(path.resolve()), "kind": kind, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    member_count = len(json.loads(manifest.read_text(encoding="utf-8"))["archiveMembers"])
    clean_install = {
        "schemaVersion": 1,
        "artifactKind": "browser_release_clean_install_check",
        "verificationLevel": "webgpu_smoke",
        "sourceMode": "release_archive",
        "status": "pass",
        "releaseCandidateEligible": True,
        "browserProduct": product,
        "platform": platform,
        "releaseArchive": {**artifact(archive, "browser_release_archive"), "byteLength": archive.stat().st_size},
        "releaseArchiveManifest": {**artifact(manifest, "browser_release_archive_manifest"), "byteLength": manifest.stat().st_size},
        "extraction": {
            "isolation": "fresh_temporary_directory",
            "archiveMemberCount": member_count,
            "extractedMemberCount": member_count,
            "borrowedMemberCount": 0,
        },
        "launchProbe": {"attempted": True, "exitCode": 0, "timedOut": False},
        "webgpuSmoke": {
            "required": True,
            "modes": ["dawn", "doe"],
            "process": {"attempted": True, "exitCode": 0, "timedOut": False},
        },
        "failures": [],
    }
    verifier_path = write_file(root / "clean-install-verifier.py", b"# fixture verifier\n")
    smoke_script = write_file(root / "webgpu-smoke.mjs", b"// fixture smoke\n")
    smoke_report = write_file(root / "clean-install-webgpu-smoke.json", b'{"reportKind": "chromium-webgpu-playwright-smoke"}\n')
    clean_install["verifier"] = artifact(verifier_path, "browser_release_clean_install_verifier")
    clean_install["webgpuSmoke"]["script"] = artifact(smoke_script, "browser_webgpu_smoke_runner")
    clean_install["webgpuSmoke"]["report"] = artifact(smoke_report, "chromium-webgpu-playwright-smoke")
    path = root / "browser-release-clean-install-check.json"
    path.write_text(json.dumps(clean_install, indent=2) + "\n", encoding="utf-8")
    return path
