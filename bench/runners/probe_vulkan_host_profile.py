#!/usr/bin/env python3
"""Emit physical Vulkan evidence for one declared host profile.

With ``--host-record`` the receipt also freezes the host identity a qualification
target needs: CPU, system memory, OS/kernel, firmware, driver packages, the selected
ICD library, GPU allocation limits, Vulkan heaps/limits and process limits. System
RAM, GPU allocation limits and per-run residency stay separate fields; this probe
measures no residency.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import resource
import shutil
import subprocess
from pathlib import Path
from typing import Any

from bench.runners.preflight_bench_host import (
    DEFAULT_VULKAN_PROFILE_POLICY,
    normalize_arch,
    probe_vulkaninfo_gpus,
    resolve_vulkan_host_profile,
    vulkan_device_matches_profile,
)


DEFAULT_RENDER_NODE = Path("/dev/dri/renderD128")
DMI_ROOT = Path("/sys/class/dmi/id")
DRM_ROOT = Path("/sys/class/drm")
TTM_PAGES_LIMIT = Path("/sys/module/ttm/parameters/pages_limit")
DMI_FIELDS = ("sys_vendor", "product_name", "board_name", "bios_vendor", "bios_version", "bios_date")
DRIVER_PACKAGES = ("mesa-vulkan-drivers", "linux-firmware", "libdrm-amdgpu1")
GPU_FIRMWARE_FIELDS = ("vbios_version", "vbios_build")
VULKAN_LIMIT_FIELDS = (
    "maxStorageBufferRange",
    "maxMemoryAllocationCount",
    "maxComputeSharedMemorySize",
    "maxComputeWorkGroupInvocations",
    "maxMemoryAllocationSize",
    "maxBufferSize",
)
RLIMITS = {"memlock": resource.RLIMIT_MEMLOCK, "addressSpace": resource.RLIMIT_AS, "openFiles": resource.RLIMIT_NOFILE}
PAGE_BYTES = os.sysconf("SC_PAGE_SIZE")
KIB = 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--profile",
        required=True,
        help="Named profile from config/vulkan-host-profiles.json.",
    )
    parser.add_argument(
        "--vulkan-profile-policy",
        type=Path,
        default=DEFAULT_VULKAN_PROFILE_POLICY,
        help="Schema-backed Vulkan host-profile policy path.",
    )
    parser.add_argument(
        "--host-record",
        action="store_true",
        help="Also freeze host, firmware, driver and resource-limit identity.",
    )
    parser.add_argument(
        "--bind",
        action="append",
        default=[],
        type=Path,
        help="Existing workload or binary artifact to bind by SHA-256 (repeatable).",
    )
    return parser.parse_args()


def probe_profile(
    *,
    profile_id: str,
    policy_path: Path,
    render_node: Path = DEFAULT_RENDER_NODE,
) -> dict[str, Any]:
    """Probe only the ICD and physical adapter declared by a host profile."""

    profile = resolve_vulkan_host_profile(policy_path, profile_id)
    host_os = "linux" if os.sys.platform.startswith("linux") else os.sys.platform
    host_arch = normalize_arch(platform.machine())
    render_accessible = (
        render_node.is_char_device()
        and os.access(render_node, os.R_OK)
        and os.access(render_node, os.W_OK)
    )
    vulkaninfo_available = shutil.which("vulkaninfo") is not None
    icd_available = profile.icd_path.is_file()
    devices: list[dict[str, str]] = []
    probe_message = "vulkaninfo or configured ICD is unavailable"
    if vulkaninfo_available and icd_available:
        devices, probe_message = probe_vulkaninfo_gpus(profile)
    matching_devices = [
        device
        for device in devices
        if vulkan_device_matches_profile(device, profile)
    ]
    ok = (
        host_os == profile.os
        and host_arch == profile.arch
        and render_accessible
        and vulkaninfo_available
        and icd_available
        and bool(matching_devices)
    )
    return {
        "schemaVersion": 1,
        "artifactKind": "vulkan-host-profile-probe",
        "ok": ok,
        "profileId": profile.id,
        "host": {"os": host_os, "arch": host_arch},
        "renderNode": {
            "path": str(render_node),
            "readableWritable": render_accessible,
        },
        "icdPath": str(profile.icd_path),
        "vendorId": profile.vendor_id,
        "matchingDevices": matching_devices,
        "probeMessage": probe_message,
    }


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        return None


def run_text(command: list[str]) -> str:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        raise ValueError(f"{' '.join(command)} failed (rc={completed.returncode})")
    return completed.stdout


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def meminfo_bytes() -> dict[str, int]:
    values: dict[str, int] = {}
    for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
        key, _, rest = line.partition(":")
        fields = rest.split()
        if key in ("MemTotal", "SwapTotal") and fields:
            values[key] = int(fields[0]) * KIB
    return values


def online_memory_bytes() -> int:
    root = Path("/sys/devices/system/memory")
    block = int(read_text(root / "block_size_bytes") or "0", 16)
    online = [entry for entry in root.glob("memory*") if read_text(entry / "online") == "1"]
    return block * len(online)


def os_release() -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in Path("/etc/os-release").read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        fields[key] = value.strip('"')
    return {"name": fields.get("PRETTY_NAME", ""), "versionId": fields.get("VERSION_ID", "")}


def package_versions() -> dict[str, str | None]:
    versions: dict[str, str | None] = {}
    for package in DRIVER_PACKAGES:
        completed = subprocess.run(["dpkg-query", "-W", "-f=${Version}", package], text=True,
                                   capture_output=True, check=False)
        versions[package] = completed.stdout.strip() if completed.returncode == 0 else None
    return versions


def gpu_card_for(device_id: str) -> Path:
    for card in sorted(DRM_ROOT.glob("card[0-9]*")):
        if read_text(card / "device" / "device") == device_id.lower():
            return card / "device"
    raise ValueError(f"no DRM card exposes PCI device {device_id}")


def selected_icd(icd_path: Path) -> dict[str, Any]:
    manifest = json.loads(icd_path.read_text(encoding="utf-8"))
    library = resolve_library(manifest["ICD"]["library_path"])
    if library is None or not library.is_file():
        raise ValueError(f"cannot resolve ICD library {manifest['ICD']['library_path']}")
    return {
        "manifestPath": str(icd_path),
        "manifestSha256": sha256_file(icd_path),
        "libraryPath": str(library),
        "librarySha256": sha256_file(library),
        "apiVersion": manifest["ICD"].get("api_version"),
    }


def parse_memory_heaps(section: str) -> list[dict[str, Any]]:
    heaps: list[dict[str, Any]] = []
    in_heaps = False
    for line in section.splitlines():
        stripped = line.strip()
        if stripped.startswith("memoryHeaps: count"):
            in_heaps = True
        elif stripped.startswith("memoryTypes:"):
            break
        elif in_heaps and re.fullmatch(r"memoryHeaps\[\d+\]:", stripped):
            heaps.append({"index": len(heaps), "sizeBytes": None, "flags": []})
        elif in_heaps and heaps and stripped.startswith("size"):
            heaps[-1]["sizeBytes"] = int(stripped.split("=", 1)[1].split()[0])
        elif in_heaps and heaps and stripped.startswith("MEMORY_HEAP_"):
            heaps[-1]["flags"].append(stripped)
    return heaps


def resolve_library(name: str) -> Path | None:
    if Path(name).is_absolute():
        return Path(name)
    for line in run_text(["ldconfig", "-p"]).splitlines():
        if line.strip().startswith(f"{name} ") and "x86-64" in line:
            return Path(line.split("=>", 1)[1].strip())
    return None


def vulkan_resources(device_name: str, icd_path: Path) -> dict[str, Any]:
    output = run_text(["env", f"VK_DRIVER_FILES={icd_path}", "vulkaninfo"])
    sections = [section for section in re.split(r"\n(?=GPU\d+:\n)", output)
                if f"= {device_name}" in section]
    if len(sections) != 1:
        raise ValueError(f"expected one vulkaninfo section for {device_name}, found {len(sections)}")
    section = sections[0]
    limits: dict[str, int] = {}
    for field in VULKAN_LIMIT_FIELDS:
        match = re.search(rf"\b{field}\s*=\s*(0x[0-9a-fA-F]+|\d+)", section)
        if match:
            limits[field] = int(match.group(1), 0)
    heaps = parse_memory_heaps(section)
    return {"limits": limits, "memoryHeaps": heaps}


def host_record(profile_receipt: dict[str, Any], binds: list[Path]) -> dict[str, Any]:
    device = profile_receipt["matchingDevices"][0]
    card = gpu_card_for(device["deviceID"])
    memory = meminfo_bytes()
    cpu_lines = [line for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines()
                 if line.startswith("model name")]
    icd_path = Path(profile_receipt["icdPath"])
    ttm_pages = read_text(TTM_PAGES_LIMIT)
    return {
        "cpu": {"model": cpu_lines[0].split(":", 1)[1].strip(), "logicalProcessors": os.cpu_count()},
        "systemMemory": {
            "onlinePhysicalBytes": online_memory_bytes(),
            "kernelMemTotalBytes": memory["MemTotal"],
            "swapTotalBytes": memory.get("SwapTotal", 0),
        },
        "operatingSystem": {**os_release(), "kernelRelease": platform.release(), "kernelVersion": platform.version()},
        "firmware": {
            "platform": {field: read_text(DMI_ROOT / field) for field in DMI_FIELDS},
            "gpu": {field: read_text(card / field) for field in GPU_FIRMWARE_FIELDS},
        },
        "driverPackages": package_versions(),
        "selectedVulkanDriver": {**selected_icd(icd_path), "deviceName": device["deviceName"],
                                 "driverInfo": device["driverInfo"], "driverVersion": device["driverVersion"]},
        "gpuAllocationLimits": {
            "dedicatedCarveoutBytes": int(read_text(card / "mem_info_vram_total") or "0"),
            "gttBytes": int(read_text(card / "mem_info_gtt_total") or "0"),
            "ttmPagesLimitBytes": int(ttm_pages) * PAGE_BYTES if ttm_pages else None,
            "vulkan": vulkan_resources(device["deviceName"], icd_path),
        },
        "processLimits": {name: list(resource.getrlimit(limit)) for name, limit in RLIMITS.items()},
        "measuredResidency": None,
        "bindings": [{"path": str(path), "sha256": sha256_file(path)} for path in binds],
    }


def main() -> int:
    args = parse_args()
    try:
        receipt = probe_profile(
            profile_id=args.profile,
            policy_path=args.vulkan_profile_policy,
        )
        if args.host_record:
            if not receipt["ok"] or len(receipt["matchingDevices"]) != 1:
                raise ValueError("host record requires exactly one matching physical device")
            receipt["hostRecord"] = host_record(receipt, args.bind)
    except ValueError as error:
        print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        return 2
    print(json.dumps(receipt, indent=2, sort_keys=True))
    return 0 if receipt["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
