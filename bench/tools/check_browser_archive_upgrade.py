"""Exercise two packaged browsers, a fresh install, and replacement on an isolated profile."""

from __future__ import annotations

import argparse
from contextlib import chdir, contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
from threading import Thread
from typing import Any, Iterator

from playwright.sync_api import Error as PlaywrightError, sync_playwright

from bench.tools import check_browser_release_clean_install as clean


@contextmanager
def profile_page(marker: str) -> Iterator[str]:
    """Serve a stable origin which tests profile continuity and an enforced page CSP."""
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path not in ("/write", "/read"):
                self.send_error(404)
                return
            # Only the baseline is allowed to create the value. All later visits read it.
            write = f'localStorage.setItem("marker", "{marker}");' if self.path == "/write" else ""
            page = f"""<!doctype html><body>
<script>document.body.dataset.forbidden = "yes"</script>
<script nonce="allowed">{write}
if (localStorage.getItem("marker") === "{marker}" && !document.body.dataset.forbidden)
  document.body.dataset.verified = "{marker}";
</script>""".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Security-Policy", "default-src 'none'; script-src 'nonce-allowed'")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def log_message(self, format: str, *args: object) -> None:
            pass

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}"
        finally:
            server.shutdown()
            thread.join()


def install_archive(archive: Path, manifest: dict[str, Any], records: dict[str, Any], install: Path) -> None:
    """Replace a stopped browser bundle inside the disposable qualification directory."""
    destination = install / manifest["appBundleName"]
    with tempfile.TemporaryDirectory(dir=install.parent) as staging:
        if sys.platform == "darwin":
            subprocess.run(["/usr/bin/ditto", "-x", "-k", archive, staging], check=True)
        else:
            clean.extract_archive(archive, Path(staging), records)
        for path, record in records.items():
            if os.access(Path(staging) / path, os.X_OK) != record["executable"]:
                raise ValueError(f"Extracted executable mode differs from the manifest for {path}")
        if destination.exists():
            shutil.rmtree(destination)  # Solely the previous bundle inside our temporary install.
        (Path(staging) / manifest["appBundleName"]).rename(destination)


def run(args: argparse.Namespace, result: dict[str, Any]) -> None:
    result["extractor"] = "macOS ditto" if sys.platform == "darwin" else "release clean-install extractor"
    policy = clean.load_json(args.platform_policy, "platform policy")
    releases = []
    for archive, manifest in ((args.old_archive, args.old_manifest), (args.new_archive, args.new_manifest)):
        record = clean.load_json(manifest, "release archive manifest")
        records, failures = clean.zip_records(archive)
        failures.extend(clean.check_static_contract(archive, record, policy, records))
        if failures:
            raise ValueError(f"Archive and manifest check failed: {failures}")
        root = record["appBundleName"]
        if Path(root).name != root or not all(p.startswith(root + "/") for p in records):
            raise ValueError("Expected one package root containing every archive member")
        releases.append((archive, record, records))
        result["archives"].append({
            "sha256": record["archive"]["sha256"], "manifestSha256": clean.sha256_file(manifest),
            "product": record["browserProduct"], "platform": record["platform"],
        })
    old, new = (r[1] for r in releases)
    if (old["browserProduct"]["productId"], old["platform"], old["appBundleName"]) != (
        new["browserProduct"]["productId"], new["platform"], new["appBundleName"]
    ):
        raise ValueError("Upgrade requires the same product, platform, and installation name")
    if releases[0][2] == releases[1][2]:
        raise ValueError("The two archives contain identical package members")
    old_version, new_version = (tuple(int(part) for part in manifest["browserProduct"]["version"].split("."))
                                for manifest in (old, new))
    if new_version < old_version:
        raise ValueError("The second archive declares an older Chromium version")
    result["browserVersionChanged"] = new_version > old_version
    marker = secrets.token_hex(16)
    with tempfile.TemporaryDirectory(prefix="fawn-standalone-upgrade-") as temp, profile_page(marker) as origin:
        root = Path(temp)
        install = root / "installed"
        home = root / "home"
        profile = root / "profile"
        for path in (install, home, profile):
            path.mkdir()
        for label, index, mode in (("install", 0, "dawn"), ("replace", 1, "doe"), ("reinstall", 1, "dawn")):
            archive, manifest, records = releases[index]
            install_archive(archive, manifest, records, install)
            browser = install / manifest["members"]["browserExecutable"]["archivePath"]
            runtime = install / manifest["members"]["doeRuntime"]["archivePath"]
            for member, role in ((browser, "browserExecutable"), (runtime, "doeRuntime")):
                if clean.sha256_file(member) != manifest["members"][role]["sha256"]:
                    raise ValueError(f"Installed {role} identity differs")
            env = {"HOME": str(home), "PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "TMPDIR": str(root)}
            if sys.platform == "linux" and os.environ.get("CHROME_DEVEL_SANDBOX"):
                env["CHROME_DEVEL_SANDBOX"] = os.environ["CHROME_DEVEL_SANDBOX"]
            version = subprocess.run([str(browser), "--version"], env=env, cwd=root, check=True,
                                     capture_output=True, text=True, timeout=args.timeout).stdout.strip()
            if manifest["browserProduct"]["version"] not in version.split():
                raise ValueError(f"Installed version probe returned {version!r}")
            with chdir(root), sync_playwright() as playwright:
                # Older product launchers already supply a start URL. Inject no second headless target.
                browser_context = playwright.chromium.launch_persistent_context(
                    str(profile), executable_path=str(browser), chromium_sandbox=True, headless=True,
                    ignore_default_args=["about:blank"], env=env, args=["--disable-gpu", f"--use-webgpu-runtime={mode}"],
                    timeout=args.timeout * 1000,
                )
                try:
                    page = browser_context.new_page()
                    page.goto(origin + ("/write" if label == "install" else "/read"), timeout=args.timeout * 1000)
                    if page.locator("body").get_attribute("data-verified") != marker:
                        raise ValueError(f"{label}: navigation, profile or page CSP probe failed")
                finally:
                    browser_context.close()
            result["steps"].append({
                "step": label, "version": version, "requestedRuntime": mode,
                "packagedRuntimeSha256": manifest["members"]["doeRuntime"]["sha256"],
                "pageCspEnforced": True, "persistentMarker": "written" if label == "install" else "recovered",
            })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("old-archive", "old-manifest", "new-archive", "new-manifest", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--platform-policy", type=Path, default=clean.DEFAULT_POLICY)
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args()
    result: dict[str, Any] = {"status": "fail", "observedAt": clean.observed_at_now(), "archives": [],
                              "steps": [], "gpuExecutionTested": False}
    try:
        run(args, result)
        result["status"] = "pass"
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError, PlaywrightError) as error:
        result["error"] = str(error)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(result.get("error", result["status"]))
    return int(result["status"] != "pass")


if __name__ == "__main__":
    raise SystemExit(main())
