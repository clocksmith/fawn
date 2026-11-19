"""Acquire, verify, and build pinned third-party WebGPU graphics workloads for Track C2.

Executes shell-free git and npm steps declared in
``bench/external-projects/webgpu-graphics-campaigns/*.workload.json``, verifies
clean source trees and exact SHA-256 file hashes for every immutable upstream
file and pinned offline module route, and emits a hash-bound ``preparation.json``
receipt. Supports ``--dry-run`` on CPU without network or GPU access.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Any

CAMPAIGN_CONTRACT_REL = "config/webgpu-graphics-proof-campaigns.json"


def sha256_bytes(data: bytes) -> str:
    """Return lowercase hex SHA-256 digest."""
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    """Return lowercase hex SHA-256 digest of a file."""
    return sha256_bytes(path.read_bytes())


def load_graphics_manifests(repo_root: Path) -> list[tuple[str, dict[str, Any]]]:
    """Load all displayed-canvas graphics workload manifests from the C2 contract."""
    contract = json.loads((repo_root / CAMPAIGN_CONTRACT_REL).read_text(encoding="utf-8"))
    manifests: list[tuple[str, dict[str, Any]]] = []
    for item in contract["workloads"]:
        if not item.get("requiresDisplayedCanvasPresentation"):
            continue
        rel = item["sourceContractPath"]
        manifest = json.loads((repo_root / rel).read_text(encoding="utf-8"))
        manifests.append((rel, manifest))
    return manifests


def build_preparation_plan(
    repo_root: Path,
    cache_root: Path,
    selected_workload: str = "all",
) -> list[dict[str, Any]]:
    """Build deterministic shell-free preparation plan for selected graphics workloads."""
    plans: list[dict[str, Any]] = []
    for manifest_rel, manifest in load_graphics_manifests(repo_root):
        wid = manifest["workloadId"]
        if selected_workload not in ("all", wid):
            continue
        checkout_dir = cache_root / wid / "upstream"
        vendor_dir = cache_root / wid / "pinned-modules"
        steps: list[dict[str, Any]] = [
            {
                "phase": "git-checkout-pinned-commit",
                "repository": manifest["upstreamRepository"],
                "commit": manifest["upstreamCommit"],
                "checkoutDir": str(checkout_dir),
            },
            {
                "phase": "verify-immutable-files",
                "files": manifest["immutableSourceFiles"],
            },
        ]
        for bstep in manifest["buildSteps"]:
            steps.append(
                {
                    "phase": "build-step",
                    "id": bstep["id"],
                    "argv": bstep["argv"],
                    "cwd": str((checkout_dir / bstep["cwdRel"]).resolve()),
                    "timeoutSeconds": bstep["timeoutSeconds"],
                }
            )
        for route in manifest["pinnedModuleRoutes"]:
            raw_url = (
                route["upstreamRepository"].replace("https://github.com/", "https://raw.githubusercontent.com/")
                + f"/{route['upstreamCommit']}/{route['upstreamPath']}"
            )
            steps.append(
                {
                    "phase": "pin-offline-module",
                    "interceptUrl": route["interceptUrl"],
                    "rawUrl": raw_url,
                    "destPath": str(vendor_dir / f"{route['sha256']}.js"),
                    "expectedSha256": route["sha256"],
                    "expectedBytes": route["bytes"],
                }
            )
        plans.append(
            {
                "workloadId": wid,
                "family": manifest["family"],
                "manifestPath": manifest_rel,
                "manifestSha256": sha256_file(repo_root / manifest_rel),
                "upstreamRepository": manifest["upstreamRepository"],
                "upstreamCommit": manifest["upstreamCommit"],
                "license": manifest["license"],
                "servedDir": str((checkout_dir / manifest["servedRoot"]).resolve()),
                "entryHtmlPath": manifest["entryHtmlPath"],
                "canvasSelector": manifest["canvasSelector"],
                "steps": steps,
            }
        )
    if not plans:
        raise ValueError(f"no graphics workload matched {selected_workload!r}")
    return plans


def verify_checked_out_files(checkout_dir: Path, expected_files: list[dict[str, Any]]) -> dict[str, str]:
    """Verify exact byte length and SHA-256 of every declared upstream file."""
    observed: dict[str, str] = {}
    for entry in expected_files:
        rel_path = entry["path"]
        fpath = checkout_dir / rel_path
        if not fpath.is_file():
            raise RuntimeError(f"missing pinned upstream file: {fpath}")
        raw = fpath.read_bytes()
        digest = sha256_bytes(raw)
        if len(raw) != int(entry["bytes"]):
            raise RuntimeError(
                f"{rel_path}: byte count mismatch ({len(raw)} != {entry['bytes']})"
            )
        if digest != entry["sha256"]:
            raise RuntimeError(
                f"{rel_path}: sha256 mismatch ({digest} != {entry['sha256']})"
            )
        observed[rel_path] = digest
    return observed


def execute_preparation(
    repo_root: Path,
    cache_root: Path,
    out_dir: Path,
    selected_workload: str = "all",
    offline: bool = False,
) -> dict[str, Any]:
    """Execute source checkout, hash verification, and build steps."""
    plans = build_preparation_plan(repo_root, cache_root, selected_workload)
    prepared_workloads: list[dict[str, Any]] = []

    for plan in plans:
        wid = plan["workloadId"]
        manifest = json.loads((repo_root / plan["manifestPath"]).read_text(encoding="utf-8"))
        checkout_dir = cache_root / wid / "upstream"
        vendor_dir = cache_root / wid / "pinned-modules"
        vendor_dir.mkdir(parents=True, exist_ok=True)

        if not (checkout_dir / ".git").exists():
            if offline:
                raise RuntimeError(f"--offline set and local clone missing: {checkout_dir}")
            checkout_dir.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                ["git", "clone", "--no-checkout", manifest["upstreamRepository"], str(checkout_dir)],
                check=True,
                timeout=600,
            )

        has_commit = (
            subprocess.run(
                ["git", "-C", str(checkout_dir), "cat-file", "-e", f"{manifest['upstreamCommit']}^{{commit}}"],
                capture_output=True,
                timeout=30,
            ).returncode
            == 0
        )
        if not has_commit:
            if offline:
                raise RuntimeError(
                    f"--offline set and commit {manifest['upstreamCommit']} missing in {checkout_dir}"
                )
            subprocess.run(
                ["git", "-C", str(checkout_dir), "fetch", "--depth", "1", "origin", manifest["upstreamCommit"]],
                check=True,
                timeout=600,
            )

        subprocess.run(
            ["git", "-C", str(checkout_dir), "checkout", "--force", manifest["upstreamCommit"]],
            check=True,
            timeout=120,
        )
        head = subprocess.check_output(
            ["git", "-C", str(checkout_dir), "rev-parse", "HEAD"],
            text=True,
            timeout=30,
        ).strip()
        if head != manifest["upstreamCommit"]:
            raise RuntimeError(f"{wid}: HEAD {head} != {manifest['upstreamCommit']}")
        dirty = subprocess.check_output(
            ["git", "-C", str(checkout_dir), "status", "--porcelain"],
            text=True,
            timeout=30,
        ).strip()
        if dirty:
            raise RuntimeError(f"{wid}: dirty upstream checkout in {checkout_dir}")

        observed_hashes = verify_checked_out_files(checkout_dir, manifest["immutableSourceFiles"])

        for bstep in manifest["buildSteps"]:
            step_cwd = (checkout_dir / bstep["cwdRel"]).resolve()
            subprocess.run(
                bstep["argv"],
                cwd=str(step_cwd),
                check=True,
                timeout=int(bstep["timeoutSeconds"]),
            )

        pinned_routes_out: list[dict[str, Any]] = []
        for route in manifest["pinnedModuleRoutes"]:
            dest = vendor_dir / f"{route['sha256']}.js"
            if not dest.exists():
                if offline:
                    raise RuntimeError(f"--offline set and pinned module missing: {dest}")
                raw_url = (
                    route["upstreamRepository"].replace(
                        "https://github.com/", "https://raw.githubusercontent.com/"
                    )
                    + f"/{route['upstreamCommit']}/{route['upstreamPath']}"
                )
                req = urllib.request.Request(raw_url, headers={"User-Agent": "fawn-c2-prepare"})
                with urllib.request.urlopen(req, timeout=30) as resp:
                    payload = resp.read()
                dest.write_bytes(payload)
            raw_mod = dest.read_bytes()
            mod_sha = sha256_bytes(raw_mod)
            if len(raw_mod) != int(route["bytes"]) or mod_sha != route["sha256"]:
                raise RuntimeError(f"{wid}: pinned module {route['interceptUrl']} failed SHA-256 check")
            pinned_routes_out.append(
                {
                    "interceptUrl": route["interceptUrl"],
                    "localPath": str(dest),
                    "sha256": mod_sha,
                    "bytes": len(raw_mod),
                    "contentType": route["contentType"],
                }
            )

        served_dir = (checkout_dir / manifest["servedRoot"]).resolve()
        entry_disk = served_dir / manifest["entryHtmlPath"].lstrip("/")
        if not entry_disk.is_file():
            raise RuntimeError(f"{wid}: built entry HTML missing at {entry_disk}")

        prepared_workloads.append(
            {
                "workloadId": wid,
                "family": manifest["family"],
                "manifestPath": plan["manifestPath"],
                "manifestSha256": plan["manifestSha256"],
                "upstreamRepository": manifest["upstreamRepository"],
                "upstreamCommit": head,
                "checkoutDir": str(checkout_dir),
                "servedDir": str(served_dir),
                "entryHtmlPath": manifest["entryHtmlPath"],
                "canvasSelector": manifest["canvasSelector"],
                "observedSourceFileHashes": observed_hashes,
                "pinnedModuleRoutes": pinned_routes_out,
            }
        )

    out_dir.mkdir(parents=True, exist_ok=True)
    receipt = {
        "schema": "fawn.c2-graphics-preparation/v1",
        "workloads": prepared_workloads,
    }
    (out_dir / "preparation.json").write_text(
        f"{json.dumps(receipt, indent=2)}\n", encoding="utf-8"
    )
    return receipt


def main(argv: list[str] | None = None) -> int:
    """CLI entrypoint."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="", help="Optional Fawn repository root.")
    parser.add_argument("--workload", default="all", help="Workload ID or 'all'.")
    parser.add_argument("--cache-root", default="bench/out/c2-graphics-cache", help="Source cache directory.")
    parser.add_argument("--out", default="bench/out/c2-graphics-prepared", help="Output receipt directory.")
    parser.add_argument("--offline", action="store_true", help="Forbid network clone/fetch.")
    parser.add_argument("--dry-run", action="store_true", help="Emit shell-free plan without executing.")
    args = parser.parse_args(argv)

    repo_root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[3]
    cache_root = (repo_root / args.cache_root).resolve()
    out_dir = (repo_root / args.out).resolve()

    if args.dry_run:
        plans = build_preparation_plan(repo_root, cache_root, args.workload)
        print(json.dumps({"schema": "fawn.c2-graphics-preparation-plan/v1", "plans": plans}, indent=2))
        return 0

    receipt = execute_preparation(
        repo_root=repo_root,
        cache_root=cache_root,
        out_dir=out_dir,
        selected_workload=args.workload,
        offline=args.offline,
    )
    print(json.dumps({"status": "prepared", "workloadCount": len(receipt["workloads"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
