"""Zero-trust offline admission checker for Doe C2 WebGPU graphics, Metal tail, and Chromium ownership campaigns.

Validates ``config/webgpu-graphics-proof-campaigns.json`` and all pinned
third-party workload manifests against repository authorities, and evaluates
candidate Vulkan, Metal, and forced-Doe Chromium receipts by recomputing source
SHA-256s, launch-flag invariants, raw latency percentiles, PNG pixel diffs, and
negative-control process outcomes rather than trusting self-reported booleans.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import random
import re
import struct
import sys
import zlib
from pathlib import Path
from typing import Any

from bench.lib.bench_utils import is_safe_repo_path, load_json_object

CONTRACT_REL_PATH = "config/webgpu-graphics-proof-campaigns.json"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
HEX40_RE = re.compile(r"^[0-9a-f]{40}$")
HEX64_RE = re.compile(r"^[0-9a-f]{64}$")

REQUIRED_WORKLOAD_FAMILIES = frozenset(
    {
        "inference_compute",
        "three_webgpu_scene",
        "image_filter_2d",
        "particles_or_fluids",
        "texture_upload_presentation",
    }
)

FORBIDDEN_GRAPHICS_FLAGS = frozenset(
    {
        "--no-sandbox",
        "--disable-setuid-sandbox",
        "--disable-vulkan-surface",
        "--headless",
        "--headless=new",
    }
)

FORBIDDEN_SANDBOX_FLAGS = frozenset(
    {
        "--no-sandbox",
        "--disable-setuid-sandbox",
    }
)


def resolve_repo_root(explicit_root: str | Path | None = None) -> Path:
    """Return the repository root path."""
    if explicit_root:
        return Path(explicit_root).resolve()
    return Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    """Compute lowercase SHA-256 hex digest of a file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linear_percentile(samples: list[float], pct: float) -> float:
    """Compute linear-interpolated percentile (0..100) from non-empty numeric samples."""
    ordered = sorted(float(x) for x in samples)
    if len(ordered) == 1:
        return ordered[0]
    rank = (pct / 100.0) * (len(ordered) - 1)
    lo = int(math.floor(rank))
    hi = int(math.ceil(rank))
    if lo == hi:
        return ordered[lo]
    frac = rank - lo
    return ordered[lo] + frac * (ordered[hi] - ordered[lo])


def compute_aa_bootstrap_calibration(
    samples_a: list[float],
    samples_b: list[float],
    *,
    seed: int = 20261012,
    resamples: int = 2000,
) -> tuple[float, float, float, float]:
    """Compute (point_drift, ci_low, ci_high, half_width) of A/A p50 ratio via seeded bootstrap."""
    p50_a = linear_percentile(samples_a, 50.0)
    p50_b = linear_percentile(samples_b, 50.0)
    point_drift = max(p50_a / p50_b, p50_b / p50_a)
    rng = random.Random(seed)
    na = len(samples_a)
    nb = len(samples_b)
    ratios: list[float] = []
    for _ in range(resamples):
        boot_a = [samples_a[rng.randrange(na)] for _ in range(na)]
        boot_b = [samples_b[rng.randrange(nb)] for _ in range(nb)]
        ratios.append(linear_percentile(boot_a, 50.0) / linear_percentile(boot_b, 50.0))
    ci_low = linear_percentile(ratios, 2.5)
    ci_high = linear_percentile(ratios, 97.5)
    half_width = max(abs(1.0 - ci_low), abs(ci_high - 1.0))
    return point_drift, ci_low, ci_high, half_width


def _paeth(left: int, above: int, upper_left: int) -> int:
    pred = left + above - upper_left
    dl = abs(pred - left)
    da = abs(pred - above)
    du = abs(pred - upper_left)
    if dl <= da and dl <= du:
        return left
    return above if da <= du else upper_left


def decode_png_rgba(png_bytes: bytes) -> tuple[int, int, bytes]:
    """Decode an 8-bit non-interlaced RGB or RGBA PNG into raw RGBA bytes."""
    if len(png_bytes) < 33 or png_bytes[:8] != PNG_SIGNATURE:
        raise ValueError("invalid PNG signature")
    offset = 8
    header: tuple[int, int, int, int] | None = None
    idat_parts: list[bytes] = []
    ended = False

    while offset < len(png_bytes):
        if offset + 12 > len(png_bytes):
            raise ValueError("truncated PNG chunk header")
        length = struct.unpack(">I", png_bytes[offset : offset + 4])[0]
        end = offset + 12 + length
        if end > len(png_bytes):
            raise ValueError("truncated PNG chunk payload")
        ctype = png_bytes[offset + 4 : offset + 8]
        data = png_bytes[offset + 8 : end - 4]
        expected_crc = struct.unpack(">I", png_bytes[end - 4 : end])[0]
        actual_crc = zlib.crc32(png_bytes[offset + 4 : end - 4]) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValueError(f"PNG CRC mismatch in {ctype.decode('latin1', errors='replace')}")

        if ctype == b"IHDR":
            if header is not None or length != 13 or offset != 8:
                raise ValueError("invalid IHDR chunk")
            width, height, depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", data)
            if width <= 0 or height <= 0 or width * height > 16_777_216:
                raise ValueError("invalid PNG dimensions")
            if depth != 8 or color_type not in (2, 6) or comp != 0 or filt != 0 or interlace != 0:
                raise ValueError("only standard 8-bit non-interlaced RGB/RGBA PNGs supported")
            header = (width, height, depth, color_type)
        elif ctype == b"IDAT":
            idat_parts.append(data)
        elif ctype == b"IEND":
            if length != 0 or end != len(png_bytes):
                raise ValueError("invalid IEND chunk or trailing bytes")
            ended = True
        elif (ctype[0] & 0x20) == 0:
            raise ValueError(f"unsupported critical PNG chunk: {ctype!r}")
        offset = end

    if header is None or not ended or not idat_parts:
        raise ValueError("incomplete PNG stream")

    width, height, _depth, color_type = header
    channels = 4 if color_type == 6 else 3
    stride = width * channels
    raw = zlib.decompress(b"".join(idat_parts))
    if len(raw) != (stride + 1) * height:
        raise ValueError("decompressed PNG length mismatch")

    rgba = bytearray(width * height * 4)
    prev_row = bytearray(stride)
    out_stride = width * 4
    for y in range(height):
        row_start = y * (stride + 1)
        ftype = raw[row_start]
        if ftype > 4:
            raise ValueError(f"unsupported PNG scanline filter {ftype}")
        scanline = raw[row_start + 1 : row_start + 1 + stride]
        if ftype == 0:
            cur_row = bytearray(scanline)
        else:
            cur_row = bytearray(scanline)
            for x in range(stride):
                left = cur_row[x - channels] if x >= channels else 0
                above = prev_row[x]
                upper_left = prev_row[x - channels] if x >= channels else 0
                if ftype == 1:
                    pred = left
                elif ftype == 2:
                    pred = above
                elif ftype == 3:
                    pred = (left + above) // 2
                else:
                    pred = _paeth(left, above, upper_left)
                cur_row[x] = (cur_row[x] + pred) & 0xFF
        if channels == 4:
            rgba[y * out_stride : (y + 1) * out_stride] = cur_row
        else:
            for px in range(width):
                dst = y * out_stride + px * 4
                src = px * 3
                rgba[dst : dst + 3] = cur_row[src : src + 3]
                rgba[dst + 3] = 255
        prev_row = cur_row

    return width, height, bytes(rgba)


def count_distinct_rgba_pixels(rgba: bytes, max_needed: int = 2) -> int:
    """Count distinct RGBA pixels up to ``max_needed`` to detect blank/solid captures."""
    seen: set[bytes] = set()
    for idx in range(0, len(rgba), 4):
        seen.add(rgba[idx : idx + 4])
        if len(seen) >= max_needed:
            return len(seen)
    return len(seen)


def compare_rgba_buffers(ref_rgba: bytes, cand_rgba: bytes) -> tuple[float, float]:
    """Compute (MAE, p99) absolute channel differences across two equal-sized RGBA buffers."""
    if len(ref_rgba) != len(cand_rgba) or len(ref_rgba) == 0:
        raise ValueError("RGBA buffer length mismatch")
    if ref_rgba == cand_rgba:
        return 0.0, 0.0
    hist = [0] * 256
    total_diff = 0
    n = len(ref_rgba)
    for a, b in zip(ref_rgba, cand_rgba):
        d = abs(a - b)
        total_diff += d
        hist[d] += 1
    mae = total_diff / n
    target_rank = math.ceil(0.99 * n)
    cum = 0
    p99 = 0.0
    for val, cnt in enumerate(hist):
        cum += cnt
        if cum >= target_rank:
            p99 = float(val)
            break
    return mae, p99


def _has_forbidden_flag(args_list: list[Any], forbidden: frozenset[str]) -> str | None:
    for raw in args_list:
        arg = str(raw)
        prefix = arg.split("=", 1)[0]
        if arg in forbidden or prefix in forbidden:
            return arg
    return None


def validate_campaign_contract(repo_root: Path) -> list[str]:
    """Validate the frozen C2 campaign contract and all pinned workload manifests."""
    failures: list[str] = []
    contract_path = repo_root / CONTRACT_REL_PATH
    if not contract_path.exists():
        return [f"missing contract: {CONTRACT_REL_PATH}"]

    contract = load_json_object(contract_path)
    for key in (
        "strategyAuthorityPath",
        "thesisAuthorityPath",
        "methodologyThresholdsPath",
        "integratedComparisonPath",
        "dopplerReceiptProducerPath",
        "preparationToolPath",
        "captureHarnessPath",
    ):
        rel = contract.get(key)
        if not is_safe_repo_path(rel):
            failures.append(f"{key} is not a safe repository-relative path: {rel!r}")
            continue
        if not (repo_root / rel).exists():
            failures.append(f"{key} target does not exist: {rel}")

    boundary = contract.get("nonConfoundingBoundary", {})
    if boundary.get("workspaceEvidenceQualifiesDoe") is not False:
        failures.append("C1 workspace evidence must never qualify Doe")
    if boundary.get("kernelWinQualifiesPageRendering") is not False:
        failures.append("isolated kernel speedups must never qualify page rendering")
    if boundary.get("forbidCrossBackendAveraging") is not True:
        failures.append("cross-backend averaging must remain forbidden")

    workloads = contract.get("workloads", [])
    seen_families: set[str] = set()
    seen_upstreams: set[str] = set()
    for item in workloads:
        wid = item.get("workloadId", "<unknown>")
        fam = item.get("family", "")
        seen_families.add(fam)

        src_rel = item.get("sourceContractPath")
        if not is_safe_repo_path(src_rel) or not (repo_root / src_rel).exists():
            failures.append(f"{wid}: missing or invalid sourceContractPath {src_rel!r}")
            continue

        oracle_ref = item.get("oracle", {}).get("referencePath")
        if not is_safe_repo_path(oracle_ref) or not (repo_root / oracle_ref).exists():
            failures.append(f"{wid}: invalid oracle.referencePath {oracle_ref!r}")

        if fam != "inference_compute":
            if item.get("requiresDisplayedCanvasPresentation") is not True:
                failures.append(f"{wid}: graphics family {fam} must require displayed canvas presentation")
            manifest = load_json_object(repo_root / src_rel)
            repo_url = str(manifest.get("upstreamRepository", ""))
            commit = str(manifest.get("upstreamCommit", ""))
            if not repo_url.startswith("https://github.com/") or not HEX40_RE.match(commit):
                failures.append(f"{wid}: graphics manifest must pin upstreamRepository and 40-hex upstreamCommit")
            else:
                seen_upstreams.add(repo_url)

            src_files = manifest.get("immutableSourceFiles", [])
            if not isinstance(src_files, list) or len(src_files) < 2:
                failures.append(f"{wid}: graphics manifest requires immutableSourceFiles with SHA-256 hashes")
            else:
                for sf in src_files:
                    if not HEX64_RE.match(str(sf.get("sha256", ""))):
                        failures.append(f"{wid}: invalid file sha256 in immutableSourceFiles: {sf!r}")

            dep_rel = manifest.get("dependencyManifestPath")
            if dep_rel is not None:
                dep_path = repo_root / str(dep_rel)
                if not is_safe_repo_path(dep_rel) or not dep_path.exists():
                    failures.append(f"{wid}: dependencyManifestPath missing: {dep_rel!r}")
                elif sha256_file(dep_path) != manifest.get("dependencyManifestSha256"):
                    failures.append(f"{wid}: dependencyManifestSha256 mismatch for {dep_rel}")

            lp = manifest.get("launchPolicy", {})
            if lp.get("headless") is not False:
                failures.append(f"{wid}: graphics manifest must set launchPolicy.headless = false")
            if lp.get("sandbox") != "enabled":
                failures.append(f"{wid}: graphics manifest must keep browser sandbox enabled")
            if lp.get("displayedSurfaceRequired") is not True:
                failures.append(f"{wid}: graphics manifest must require displayed surface")
            all_declared_args = list(lp.get("commonBrowserArgs", []))
            for plat_args in (lp.get("platformBrowserArgs") or {}).values():
                if isinstance(plat_args, list):
                    all_declared_args.extend(plat_args)
            if isinstance(manifest.get("browserArgs"), list):
                all_declared_args.extend(manifest["browserArgs"])
            bad_arg = _has_forbidden_flag(all_declared_args, FORBIDDEN_GRAPHICS_FLAGS)
            if bad_arg:
                failures.append(f"{wid}: manifest declares forbidden graphics launch flag {bad_arg!r}")

    missing_families = REQUIRED_WORKLOAD_FAMILIES - seen_families
    if missing_families:
        failures.append(f"missing required workload families: {sorted(missing_families)}")
    if len(seen_upstreams) < 4:
        failures.append(
            f"expected 4 distinct pinned third-party graphics repositories, found {len(seen_upstreams)}"
        )

    backends = {b.get("backendId"): b for b in contract.get("backends", [])}
    if "amd-vulkan" not in backends or "apple-metal" not in backends:
        failures.append("both amd-vulkan and apple-metal backends must be declared")
    else:
        metal = backends["apple-metal"]
        if metal.get("dependsOnFrozenBackendId") != "amd-vulkan":
            failures.append("apple-metal must sequence after frozen amd-vulkan checkpoint")
        obligations = metal.get("tailRegressionObligations", [])
        if not any(o.get("obligationId") == "onnx-runtime-metal-tail-p95-p99" for o in obligations):
            failures.append("apple-metal must retain onnx-runtime-metal-tail-p95-p99 obligation")

    ownership = contract.get("chromiumOwnershipGate", {})
    maint_rel = ownership.get("maintainabilityAuthorityPath")
    if not is_safe_repo_path(maint_rel) or not (repo_root / maint_rel).exists():
        failures.append(f"invalid maintainabilityAuthorityPath: {maint_rel!r}")
    integrated_path = repo_root / str(contract.get("integratedComparisonPath", ""))
    if integrated_path.exists():
        integrated = load_json_object(integrated_path)
        expected_arms = [arm.get("name") for arm in integrated.get("arms", [])]
        if ownership.get("threeWayArmsInOrder") != expected_arms:
            failures.append("threeWayArmsInOrder does not match integrated comparison arms")

    return failures


def _expected_source_hashes_for_workload(repo_root: Path, wspec: dict[str, Any]) -> dict[str, str]:
    manifest = load_json_object(repo_root / wspec["sourceContractPath"])
    if wspec["family"] == "inference_compute":
        return {f["path"]: f["sha256"] for f in manifest.get("model", {}).get("files", [])}
    return {f["path"]: f["sha256"] for f in manifest.get("immutableSourceFiles", [])}


def _resolve_evidence_path(base_dir: Path, raw_path: Any) -> Path | None:
    if not isinstance(raw_path, str) or not raw_path.strip():
        return None
    p = Path(raw_path)
    return p if p.is_absolute() else (base_dir / p).resolve()


def evaluate_backend_campaign_receipt(
    contract: dict[str, Any],
    thresholds: dict[str, Any],
    receipt: dict[str, Any],
    repo_root: Path | None = None,
    evidence_base_dir: Path | None = None,
) -> list[str]:
    """Evaluate a Vulkan or Metal campaign receipt from raw artifacts, hashes, PNGs, and samples."""
    failures: list[str] = []
    root = resolve_repo_root(repo_root)
    base_dir = (evidence_base_dir or root).resolve()
    controls = contract["comparatorControls"]
    suspicious_ratio = float(thresholds["reliability"]["suspiciousSpeedupRatio"])
    min_samples = int(thresholds["claimabilityDefaults"]["localMinTimedSamples"])

    backend_id = receipt.get("backendId")
    backends_by_id = {b["backendId"]: b for b in contract["backends"]}
    if backend_id not in backends_by_id:
        return [f"unknown backendId: {backend_id!r}"]
    backend_spec = backends_by_id[backend_id]

    observed_backends = receipt.get("observedBackends")
    if isinstance(observed_backends, list) and set(observed_backends) != {backend_id}:
        failures.append(f"cross-backend pooling is forbidden: observedBackends={observed_backends!r}")

    if backend_spec["dependsOnFrozenBackendId"]:
        prior_id = backend_spec["dependsOnFrozenBackendId"]
        ckpt_path = _resolve_evidence_path(base_dir, receipt.get("priorFrozenCheckpointPath"))
        ckpt_sha = str(receipt.get("priorFrozenCheckpointSha256", ""))
        if ckpt_path is None or not ckpt_path.is_file():
            failures.append(f"{backend_id} requires existing frozen {prior_id} checkpoint file on disk")
        elif not HEX64_RE.match(ckpt_sha) or sha256_file(ckpt_path) != ckpt_sha:
            failures.append(f"{backend_id}: priorFrozenCheckpointSha256 does not match file on disk")
        else:
            ckpt_obj = load_json_object(ckpt_path)
            if ckpt_obj.get("backendId") != prior_id:
                failures.append(f"{backend_id}: frozen checkpoint backendId != {prior_id}")

    # Verify Dawn/Doe build & runtime parity from concrete metadata and loaded libraries
    dawn_build = receipt.get("dawnBuild") or {}
    doe_build = receipt.get("doeBuild") or {}
    dawn_rev = str(dawn_build.get("dawnRevision", ""))
    doe_base_rev = str(doe_build.get("dawnBaselineRevision", ""))
    if not HEX40_RE.match(dawn_rev) or dawn_rev != doe_base_rev:
        failures.append(
            f"Dawn baseline must be source-matched by 40-hex revision ({dawn_rev!r} != {doe_base_rev!r})"
        )
    if not HEX64_RE.match(str(dawn_build.get("browserExecutableSha256", ""))) or not HEX64_RE.match(
        str(doe_build.get("browserExecutableSha256", ""))
    ):
        failures.append("both dawnBuild and doeBuild require 64-hex browserExecutableSha256")
    if (
        dawn_build.get("validationMode") != doe_build.get("validationMode")
        or not dawn_build.get("validationMode")
        or dawn_build.get("cachePolicy") != "per-process-empty"
        or doe_build.get("cachePolicy") != "per-process-empty"
        or dawn_build.get("warmupFrames") != doe_build.get("warmupFrames")
        or not isinstance(dawn_build.get("warmupFrames"), int)
    ):
        failures.append("validationMode, per-process-empty cachePolicy, and warmupFrames must match Dawn")

    for label, b_obj in (("dawnBuild", dawn_build), ("doeBuild", doe_build)):
        b_args = b_obj.get("actualBrowserArgs")
        if not isinstance(b_args, list) or len(b_args) == 0:
            failures.append(f"{label}: missing actualBrowserArgs list")
        else:
            bad = _has_forbidden_flag(b_args, FORBIDDEN_GRAPHICS_FLAGS)
            if bad:
                failures.append(f"{label}: actualBrowserArgs contains forbidden flag {bad!r}")

    expected_doe_lib = str(doe_build.get("expectedDoeLibrarySha256", ""))
    dawn_lib_sha = str(dawn_build.get("dawnLibrarySha256", ""))
    loaded_doe_libs = doe_build.get("observedLoadedLibraries") or []
    loaded_shas = {str(x.get("sha256", "")) for x in loaded_doe_libs if isinstance(x, dict)}
    loaded_paths = [str(x.get("path", "")) for x in loaded_doe_libs if isinstance(x, dict)]
    if not HEX64_RE.match(expected_doe_lib) or expected_doe_lib not in loaded_shas:
        failures.append("doeBuild.observedLoadedLibraries did not load expectedDoeLibrarySha256")
    if (dawn_lib_sha and dawn_lib_sha in loaded_shas) or any(
        "dawn" in p.lower() for p in loaded_paths
    ):
        failures.append("fallback to Dawn detected in doeBuild.observedLoadedLibraries")

    # Recompute A/A stability and empirical noise floor via 8-process seeded bootstrap
    aa_samples = receipt.get("aaControlSamplesMs") or {}
    aa_a = aa_samples.get("A")
    aa_b = aa_samples.get("B")
    min_aa = int(controls["minAaRunsPerBackend"])
    max_aa_ratio = float(controls["maxAaP50DriftRatio"])
    measured_aa_half_width = 0.0
    if (
        not isinstance(aa_a, list)
        or not isinstance(aa_b, list)
        or len(aa_a) < min_aa
        or len(aa_b) < min_aa
        or any(not isinstance(x, (int, float)) or x <= 0 for x in (*aa_a, *aa_b))
    ):
        failures.append(f"aaControlSamplesMs requires >= {min_aa} positive samples per label A and B")
    else:
        aa_drift, ci_low, ci_high, measured_aa_half_width = compute_aa_bootstrap_calibration(aa_a, aa_b)
        if aa_drift > max_aa_ratio or ci_low < (1.0 / max_aa_ratio) or ci_high > max_aa_ratio:
            failures.append(
                f"A/A bootstrap 95% CI [{ci_low:.4f}, {ci_high:.4f}] (drift={aa_drift:.4f}) "
                f"exceeds [{1.0 / max_aa_ratio:.4f}, {max_aa_ratio:.4f}]"
            )

    decoded_png_cache: dict[Path, tuple[int, int, bytes]] = {}
    diff_pair_cache: dict[tuple[Path, Path], tuple[float, float]] = {}

    def _load_decoded_png(png_file: Path) -> tuple[int, int, bytes]:
        cached = decoded_png_cache.get(png_file)
        if cached is not None:
            return cached
        decoded = decode_png_rgba(png_file.read_bytes())
        decoded_png_cache[png_file] = decoded
        return decoded

    def _compare_pair(p1: Path, b1: bytes, p2: Path, b2: bytes) -> tuple[float, float]:
        key = (p1, p2)
        if key not in diff_pair_cache:
            diff_pair_cache[key] = compare_rgba_buffers(b1, b2)
        return diff_pair_cache[key]

    rows_by_id = {r.get("workloadId"): r for r in receipt.get("workloadRows", [])}
    for wspec in contract["workloads"]:
        wid = wspec["workloadId"]
        row = rows_by_id.get(wid)
        if row is None:
            failures.append(f"missing required workload row: {wid}")
            continue

        # 1. Verify unchanged application via concrete SHA-256 source-hash map
        expected_hashes = _expected_source_hashes_for_workload(root, wspec)
        observed_hashes = row.get("observedSourceFileHashes")
        if not isinstance(observed_hashes, dict):
            failures.append(f"{wid}: missing observedSourceFileHashes map")
        else:
            for rel_file, exp_sha in expected_hashes.items():
                if observed_hashes.get(rel_file) != exp_sha:
                    failures.append(f"{wid}: source hash mismatch for {rel_file}")

        # 2. Verify independent correctness oracle from actual outputs or decoded PNG files
        if wspec["family"] == "inference_compute":
            src_contract = load_json_object(root / wspec["sourceContractPath"])
            req_case_ids = [c["id"] for c in src_contract.get("cases", [])]
            obs_cases = row.get("observedCases") or {}
            ref_cases = row.get("referenceCases") or {}
            for cid in req_case_ids:
                oc = obs_cases.get(cid)
                rc = ref_cases.get(cid)
                if not isinstance(oc, dict) or not isinstance(rc, dict):
                    failures.append(f"{wid}: missing observed/reference output for case {cid}")
                    continue
                for field in ("promptTokenIds", "generatedTokenIds", "decodedText", "stopReason"):
                    if oc.get(field) != rc.get(field) or oc.get(field) in (None, "", []):
                        failures.append(f"{wid}: exact token/text oracle mismatch on {cid}.{field}")
        else:
            if row.get("timingMethod") in controls["forbiddenPresentationSubstitutes"]:
                failures.append(f"{wid}: forbidden timing substitute {row.get('timingMethod')!r}")
            manifest = load_json_object(root / wspec["sourceContractPath"])
            req_frames = manifest["captureFrames"]["pinnedFrameNumbers"]
            exp_w = int(wspec["viewport"]["width"])
            exp_h = int(wspec["viewport"]["height"])
            max_mae = float(wspec["oracle"]["maxChannelMae"])
            max_p99 = float(wspec["oracle"]["maxChannelP99"])
            frames_by_num = {
                int(fc["frameNumber"]): fc
                for fc in (row.get("frameComparisons") or [])
                if isinstance(fc, dict) and isinstance(fc.get("frameNumber"), int)
            }
            for fnum in req_frames:
                fc = frames_by_num.get(int(fnum))
                if fc is None:
                    failures.append(f"{wid}: missing pinned frame capture {fnum}")
                    continue
                if int(fc.get("submissionSequence", 0)) < int(fnum):
                    failures.append(f"{wid}: frame {fnum} missing queue submission sequence")
                dawn_png = _resolve_evidence_path(base_dir, fc.get("dawnPngPath"))
                doe_png = _resolve_evidence_path(base_dir, fc.get("doePngPath"))
                if dawn_png is None or not dawn_png.is_file() or doe_png is None or not doe_png.is_file():
                    failures.append(f"{wid}: frame {fnum} PNG capture files missing on disk")
                    continue
                try:
                    dw, dh, d_rgba = _load_decoded_png(dawn_png)
                    cw, ch, c_rgba = _load_decoded_png(doe_png)
                except ValueError as exc:
                    failures.append(f"{wid}: frame {fnum} invalid PNG ({exc})")
                    continue
                if (dw, dh) != (exp_w, exp_h) or (cw, ch) != (exp_w, exp_h):
                    failures.append(
                        f"{wid}: frame {fnum} PNG dimensions {(cw, ch)} != {(exp_w, exp_h)}"
                    )
                    continue
                if count_distinct_rgba_pixels(d_rgba) < 2 or count_distinct_rgba_pixels(c_rgba) < 2:
                    failures.append(f"{wid}: frame {fnum} displayed canvas capture is blank or uniform")
                    continue
                mae, p99 = _compare_pair(dawn_png, d_rgba, doe_png, c_rgba)
                if mae > max_mae or p99 > max_p99:
                    failures.append(
                        f"{wid}: frame {fnum} RGBA diff (mae={mae:.3f}, p99={p99:.1f}) exceeds ({max_mae}, {max_p99})"
                    )

        # 3. Verify timing phases and resources
        phases = row.get("timingPhasesMs", {})
        for req_phase in controls["requiredTimingPhases"]:
            if req_phase == "displayedPresentationMs" and not wspec.get("requiresDisplayedCanvasPresentation"):
                continue
            val = phases.get(req_phase)
            if not isinstance(val, (int, float)) or val <= 0:
                failures.append(f"{wid}: missing or non-positive timing phase {req_phase}")

        resources = row.get("resources", {})
        for req_res in controls["requiredResourceMetrics"]:
            if not isinstance(resources.get(req_res), (int, float)):
                failures.append(f"{wid}: missing resource metric {req_res}")
        if int(resources.get("failOrSkipCount", 1)) != 0:
            failures.append(f"{wid}: non-zero failOrSkipCount")

        # 4. Recompute p50 and p95 win ratios from raw sample arrays
        dawn_samples = row.get("dawnSamplesMs")
        doe_samples = row.get("doeSamplesMs")
        if (
            not isinstance(dawn_samples, list)
            or not isinstance(doe_samples, list)
            or len(dawn_samples) < min_samples
            or len(doe_samples) < min_samples
            or any(not isinstance(x, (int, float)) or x <= 0 for x in (*dawn_samples, *doe_samples))
        ):
            failures.append(f"{wid}: dawnSamplesMs and doeSamplesMs require >= {min_samples} positive samples")
            continue

        dawn_p50 = linear_percentile(dawn_samples, 50.0)
        doe_p50 = linear_percentile(doe_samples, 50.0)
        dawn_p95 = linear_percentile(dawn_samples, 95.0)
        doe_p95 = linear_percentile(doe_samples, 95.0)
        ratio_p50 = dawn_p50 / doe_p50
        ratio_p95 = dawn_p95 / doe_p95
        req_p50 = max(float(wspec["minimumWinRatioP50"]), 1.0 + 2.0 * measured_aa_half_width)
        req_p95 = max(float(wspec["minimumWinRatioP95"]), 1.0 + 2.0 * measured_aa_half_width)
        if ratio_p50 < req_p50:
            failures.append(f"{wid}: recomputed p50 win ratio {ratio_p50:.3f} < required {req_p50:.3f}")
        if ratio_p95 < req_p95:
            failures.append(f"{wid}: recomputed p95 win ratio {ratio_p95:.3f} < required {req_p95:.3f}")

        if ratio_p50 >= suspicious_ratio or ratio_p95 >= suspicious_ratio:
            audit_path = _resolve_evidence_path(base_dir, row.get("fairnessAuditArtifactPath"))
            audit_sha = str(row.get("fairnessAuditSha256", ""))
            if audit_path is None or not audit_path.is_file() or sha256_file(audit_path) != audit_sha:
                failures.append(
                    f"{wid}: speedup >= {suspicious_ratio}x requires hash-verified fairnessAuditArtifactPath on disk"
                )
            else:
                audit_obj = load_json_object(audit_path)
                if (
                    audit_obj.get("workloadId") != wid
                    or not str(audit_obj.get("attributedMechanism", "")).strip()
                    or int(audit_obj.get("verifiedCompletedDispatches", 0)) <= 0
                ):
                    failures.append(f"{wid}: fairness audit artifact is incomplete")

    # 5. Recompute Metal ONNX p95/p99 tail regression from raw tail arrays + retained adverse run file
    tail_reports = {t.get("obligationId"): t for t in receipt.get("tailObligations", [])}
    for obligation in backend_spec.get("tailRegressionObligations", []):
        oid = obligation["obligationId"]
        treport = tail_reports.get(oid)
        if treport is None:
            failures.append(f"{backend_id}: missing tail obligation report {oid}")
            continue
        adverse_path = _resolve_evidence_path(base_dir, treport.get("adverseRunPath"))
        adverse_sha = str(treport.get("adverseRunSha256", ""))
        if adverse_path is None or not adverse_path.is_file() or sha256_file(adverse_path) != adverse_sha:
            failures.append(f"{oid}: retained adverse Metal run file missing or SHA-256 mismatch")

        dawn_tail = treport.get("dawnTailSamplesMs")
        doe_tail = treport.get("doeTailSamplesMs")
        if (
            not isinstance(dawn_tail, list)
            or not isinstance(doe_tail, list)
            or len(dawn_tail) < min_samples
            or len(doe_tail) < min_samples
            or any(not isinstance(x, (int, float)) or x <= 0 for x in (*dawn_tail, *doe_tail))
        ):
            failures.append(f"{oid}: dawnTailSamplesMs and doeTailSamplesMs require >= {min_samples} samples")
            continue

        dawn_p95_t = linear_percentile(dawn_tail, 95.0)
        doe_p95_t = linear_percentile(doe_tail, 95.0)
        dawn_p99_t = linear_percentile(dawn_tail, 99.0)
        doe_p99_t = linear_percentile(doe_tail, 99.0)
        status = treport.get("disposition")
        if status == "fixed-on-current-code":
            if doe_p95_t > dawn_p95_t * 1.05 or doe_p99_t > dawn_p99_t * 1.05:
                failures.append(
                    f"{oid}: tail regression still present on p95/p99 ({doe_p95_t:.2f}/{doe_p99_t:.2f} vs "
                    f"{dawn_p95_t:.2f}/{dawn_p99_t:.2f}); mean cannot hide tail loss"
                )
        elif status != "unqualified-scope-disclosed" or not str(treport.get("unqualifiedScope", "")).strip():
            failures.append(f"{oid}: invalid disposition {status!r} or missing unqualifiedScope")

    return failures


def evaluate_chromium_ownership_receipt(
    contract: dict[str, Any],
    receipt: dict[str, Any],
    evidence_base_dir: Path | None = None,
) -> list[str]:
    """Verify forced-Doe Chromium page ownership and disabled-Doe negative-control failure from raw evidence."""
    failures: list[str] = []
    base_dir = (evidence_base_dir or resolve_repo_root()).resolve()
    gate = contract["chromiumOwnershipGate"]

    pos = receipt.get("positiveRun") or {}
    neg = receipt.get("disabledDoeNegativeControlRun") or {}

    pos_args = pos.get("actualBrowserArgs")
    if not isinstance(pos_args, list) or len(pos_args) == 0:
        failures.append("positiveRun: missing actualBrowserArgs")
    else:
        bad_sandbox = _has_forbidden_flag(pos_args, FORBIDDEN_SANDBOX_FLAGS)
        if bad_sandbox:
            failures.append(f"Chromium sandbox must remain enabled (found {bad_sandbox!r})")

    if pos.get("exitCode") != 0:
        failures.append(f"positiveRun: non-zero exitCode {pos.get('exitCode')!r}")

    doe_lib_sha = str(pos.get("expectedDoeLibrarySha256", ""))
    pos_libs = pos.get("observedLoadedLibraries") or []
    pos_lib_shas = {str(x.get("sha256", "")) for x in pos_libs if isinstance(x, dict)}
    pos_lib_paths = [str(x.get("path", "")) for x in pos_libs if isinstance(x, dict)]
    if not HEX64_RE.match(doe_lib_sha) or doe_lib_sha not in pos_lib_shas:
        failures.append("positiveRun: expectedDoeLibrarySha256 not observed in loaded libraries")
    if any("dawn" in p.lower() for p in pos_lib_paths):
        failures.append("silent fallback to Dawn detected in positiveRun loaded libraries")

    trace_events = pos.get("lifecycleTrace") or []
    observed_stages: set[str] = set()
    for ev in trace_events:
        if not isinstance(ev, dict):
            continue
        if ev.get("ownerLibrarySha256") != doe_lib_sha:
            continue
        stage = str(ev.get("stage", ""))
        if stage == "canvas_presentation":
            png_path = _resolve_evidence_path(base_dir, ev.get("presentedPngPath"))
            if png_path is None or not png_path.is_file():
                failures.append("positiveRun: canvas_presentation PNG missing on disk")
                continue
            try:
                _w, _h, rgba = decode_png_rgba(png_path.read_bytes())
                if count_distinct_rgba_pixels(rgba) < 2:
                    failures.append("positiveRun: canvas_presentation PNG is blank/uniform")
                    continue
            except ValueError as exc:
                failures.append(f"positiveRun: invalid canvas_presentation PNG ({exc})")
                continue
        if ev.get("handleOrSequence") not in (None, "", 0):
            observed_stages.add(stage)

    missing = [s for s in gate["requiredLifecycleStages"] if s not in observed_stages]
    if missing:
        failures.append(f"missing Doe-owned page lifecycle stages: {missing}")

    # Disabled-Doe negative control must fail visibly without Dawn fallback
    if not str(neg.get("disableMechanism", "")).strip():
        failures.append("disabledDoeNegativeControlRun: missing disableMechanism")
    neg_failed = (neg.get("exitCode") not in (None, 0)) or bool(str(neg.get("observedWebgpuError", "")).strip())
    if not neg_failed or int(neg.get("presentedFrames", 1)) != 0:
        failures.append("disabled-Doe negative control did not fail visibly with 0 presented frames")
    neg_libs = neg.get("observedLoadedLibraries") or []
    if any("dawn" in str(x.get("path", "")).lower() for x in neg_libs if isinstance(x, dict)):
        failures.append("disabled-Doe negative control fell back to Dawn")

    maint_path = _resolve_evidence_path(base_dir, receipt.get("maintainabilityReceiptPath"))
    maint_sha = str(receipt.get("maintainabilityReceiptSha256", ""))
    if maint_path is None or not maint_path.is_file() or sha256_file(maint_path) != maint_sha:
        failures.append("maintainabilityGatesReceipt missing on disk or SHA-256 mismatch")

    if receipt.get("ranThreeWayComparison") is True and failures:
        failures.append("three-way comparison ran before page ownership passed")
    return failures


def main(argv: list[str] | None = None) -> int:
    """Run CLI contract check and optional receipt verification."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="", help="Optional Fawn repository root.")
    parser.add_argument("--verify-backend-receipt", default="", help="Optional backend campaign receipt JSON.")
    parser.add_argument("--verify-ownership-receipt", default="", help="Optional Chromium ownership receipt JSON.")
    args = parser.parse_args(argv)

    root = resolve_repo_root(args.root or None)
    errors = validate_campaign_contract(root)
    contract = load_json_object(root / CONTRACT_REL_PATH) if not errors else {}

    if not errors and args.verify_backend_receipt:
        rpath = Path(args.verify_backend_receipt).resolve()
        thresholds = load_json_object(root / contract["methodologyThresholdsPath"])
        errors.extend(
            evaluate_backend_campaign_receipt(
                contract, thresholds, load_json_object(rpath), repo_root=root, evidence_base_dir=rpath.parent
            )
        )
    if not errors and args.verify_ownership_receipt:
        opath = Path(args.verify_ownership_receipt).resolve()
        errors.extend(
            evaluate_chromium_ownership_receipt(
                contract, load_json_object(opath), evidence_base_dir=opath.parent
            )
        )

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1
    print("ok: C2 WebGPU graphics, Metal tail, and Chromium ownership contract validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
