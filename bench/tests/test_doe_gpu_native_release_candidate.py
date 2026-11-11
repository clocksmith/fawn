from __future__ import annotations

import copy
import hashlib
import io
import json
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

import jsonschema

from bench.gates.doe_gpu_native_release_candidate_gate import (
    EXPECTED_OUTPUT_SHA256,
    PLATFORM_PACKAGES,
    library_path_in_package,
    package_directory,
    platform_pin_failures,
    sha256_json,
    validate_candidate_set,
)


ROOT = Path(__file__).resolve().parents[2]
REPORT_DIR = ROOT / "reports/benchmarks/amd-vulkan/20260828T152721Z"
SCHEMA_PATH = ROOT / "config/doe-gpu-native-release-candidate.schema.json"
RUNTIMES = ("node", "bun", "electron")
RECORDED_IMPLEMENTATION_COMMIT = "cbb9be04b03f52358521c67bd7f014619fc4fe5c"
REPORTS = {
    runtime: REPORT_DIR / f"doe-gpu-{runtime}-native-release-candidate.json"
    for runtime in RUNTIMES
}


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def git_blob(commit: str, path: str) -> bytes:
    return subprocess.run(
        ["git", "show", f"{commit}:{path}"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout


def write_tarball(path: Path, members: dict[str, bytes]) -> None:
    with tarfile.open(path, mode="w:gz") as archive:
        for name, payload in sorted(members.items()):
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = 0o644
            member.mtime = 0
            archive.addfile(member, io.BytesIO(payload))


def write_json(path: Path, payload: object) -> None:
    path.write_text(f"{json.dumps(payload, indent=2)}\n", encoding="utf-8")


def build_retained_bundle(bundle_root: Path) -> dict[str, Path]:
    reports = {
        runtime: json.loads(path.read_text(encoding="utf-8"))
        for runtime, path in REPORTS.items()
    }
    commit = RECORDED_IMPLEMENTATION_COMMIT
    packages_root = bundle_root / "packages"
    packages_root.mkdir(parents=True)
    wrapper_path = packages_root / "doe-gpu-0.5.0.tgz"
    platform_path = packages_root / "doe-gpu-linux-x64-0.5.0.tgz"
    wrapper_members = {
        "package/package.json": git_blob(commit, "packages/doe-gpu/package.json"),
        **{
            f"package/examples/{runtime}-first-kernel.mjs": git_blob(
                commit,
                f"packages/doe-gpu/examples/{runtime}-first-kernel.mjs",
            )
            for runtime in RUNTIMES
        },
    }
    addon = b"synthetic-doe-addon\n"
    metadata = b'{"synthetic":true}\n'
    library = b"synthetic-doe-runtime\n"
    platform_members = {
        "package/package.json": git_blob(
            commit,
            "packages/doe-gpu-linux-x64/package.json",
        ),
        "package/bin/doe_napi.node": addon,
        "package/bin/doe-build-metadata.json": metadata,
        "package/bin/libwebgpu_doe.so": library,
    }
    write_tarball(wrapper_path, wrapper_members)
    write_tarball(platform_path, platform_members)
    wrapper_package = {
        "id": "doe-gpu@0.5.0",
        "bytes": wrapper_path.stat().st_size,
        "sha256": sha256(wrapper_path.read_bytes()),
        "artifactPath": "packages/doe-gpu-0.5.0.tgz",
    }
    platform_package = {
        "id": "doe-gpu-linux-x64@0.5.0",
        "bytes": platform_path.stat().st_size,
        "sha256": sha256(platform_path.read_bytes()),
        "artifactPath": "packages/doe-gpu-linux-x64-0.5.0.tgz",
        "stagedLibrarySha256": sha256(library),
    }

    candidate_paths: dict[str, Path] = {}
    for runtime, report in reports.items():
        report["schemaVersion"] = 2
        report["sourceCommit"] = commit
        report["packages"] = {
            "wrapper": copy.deepcopy(wrapper_package),
            "platform": copy.deepcopy(platform_package),
        }
        report["implementation"]["stagedAddon"]["sha256"] = sha256(addon)
        report["implementation"]["stagedBuildMetadata"]["sha256"] = sha256(metadata)

        old_reliability_path = ROOT / report["reliabilityEvidence"]["path"]
        reliability = json.loads(old_reliability_path.read_text(encoding="utf-8"))
        reliability["packages"] = {
            "wrapper": {
                key: wrapper_package[key] for key in ("id", "bytes", "sha256")
            },
            "platform": {
                key: platform_package[key]
                for key in ("id", "bytes", "sha256", "stagedLibrarySha256")
            },
        }
        reliability_path = (
            bundle_root / f"doe-gpu-{runtime}-native-release-candidate.reliability.json"
        )
        write_json(reliability_path, reliability)
        report["reliabilityEvidence"] = {
            "path": reliability_path.name,
            "sha256": sha256(reliability_path.read_bytes()),
            "artifactKind": reliability["artifactKind"],
            "status": reliability["status"],
        }
        candidate_path = bundle_root / f"doe-gpu-{runtime}-native-release-candidate.json"
        write_json(candidate_path, report)
        candidate_paths[runtime] = candidate_path
    return candidate_paths


ALL_RUNTIMES = ("node", "bun", "electron")


class DoeGpuNativeReleaseCandidateTests(unittest.TestCase):
    def test_node_only_candidate_passes_when_it_declares_only_node(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node",))
            self.assertEqual(failures, [])

    def test_gate_rejects_a_declaration_without_node(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(
                ROOT, [candidates["bun"], candidates["electron"]], "linux", "x64", ("bun", "electron")
            )
            self.assertTrue(any("omit the mandatory node runtime" in item for item in failures))

    def test_gate_rejects_unknown_or_repeated_declarations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node", "deno"))
            self.assertTrue(any("are not supported" in item for item in failures))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node", "node"))
            self.assertTrue(any("repeat an entry" in item for item in failures))

    def test_gate_rejects_a_declared_runtime_without_its_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node", "bun"))
            self.assertTrue(any("candidate set runtime mismatch" in item for item in failures))

    def test_retained_candidate_set_passes_independent_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertEqual(failures, [])

    def test_gate_rejects_missing_retained_package_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle_root = Path(temporary)
            candidates = build_retained_bundle(bundle_root)
            (bundle_root / "packages/doe-gpu-0.5.0.tgz").unlink()
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertTrue(any("referenced file is missing" in item for item in failures))

    def test_gate_rejects_receipt_digest_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            node_path = candidates["node"]
            node = json.loads(node_path.read_text(encoding="utf-8"))
            node["governedReplay"]["primaryReceipt"]["execution"]["durationMs"] += 1
            write_json(node_path, node)
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertIn(
                "doe-gpu-node-native-release-candidate.json: primary receipt digest mismatch",
                failures,
            )

    def test_gate_rejects_incomplete_runtime_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(
                ROOT,
                [candidates["node"], candidates["bun"]],
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertTrue(any("candidate set runtime mismatch" in item for item in failures))

    def test_gate_rejects_source_commit_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            for candidate_path in candidates.values():
                candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
                candidate["sourceCommit"] = "d255ff815530daae5254dc05e3a98f52d224f442"
                write_json(candidate_path, candidate)
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertTrue(any("runner: SHA-256 mismatch" in item for item in failures))
            self.assertTrue(
                any("candidateFixture: source path is absent" in item for item in failures)
            )

    def test_gate_rejects_reliability_join_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle_root = Path(temporary)
            candidates = build_retained_bundle(bundle_root)
            node_path = candidates["node"]
            node = json.loads(node_path.read_text(encoding="utf-8"))
            reliability_path = bundle_root / node["reliabilityEvidence"]["path"]
            reliability = json.loads(reliability_path.read_text(encoding="utf-8"))
            reliability["packages"]["wrapper"]["sha256"] = "0" * 64
            write_json(reliability_path, reliability)
            node["reliabilityEvidence"]["sha256"] = sha256(reliability_path.read_bytes())
            write_json(node_path, node)
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertIn(
                "doe-gpu-node-native-release-candidate.json: reliability: "
                "wrapper sha256 mismatch",
                failures,
            )

    def test_gate_rejects_hidden_reliability_trial_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle_root = Path(temporary)
            candidates = build_retained_bundle(bundle_root)
            node_path = candidates["node"]
            node = json.loads(node_path.read_text(encoding="utf-8"))
            reliability_path = bundle_root / node["reliabilityEvidence"]["path"]
            reliability = json.loads(reliability_path.read_text(encoding="utf-8"))
            reliability["trials"][0]["exitCode"] = 1
            write_json(reliability_path, reliability)
            node["reliabilityEvidence"]["sha256"] = sha256(reliability_path.read_bytes())
            write_json(node_path, node)
            failures = validate_candidate_set(
                ROOT,
                list(candidates.values()),
                "linux",
                "x64",
                ALL_RUNTIMES,
            )
            self.assertIn(
                "doe-gpu-node-native-release-candidate.json: reliability: "
                "bounded process trial failed: sequential-0",
                failures,
            )

    def test_empty_adapter_architecture_is_admitted_but_must_stay_a_string(self) -> None:
        # WebGPU permits an empty architecture; the C runtime reports none on
        # Vulkan and D3D12. Every receipt and digest stays bound to it.
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            for candidate_path in candidates.values():
                candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
                governed = candidate["governedReplay"]
                governed["adapterInfo"]["architecture"] = ""
                governed["adapterInfoSha256"] = sha256_json(governed["adapterInfo"])
                for name in ("primaryReceipt", "replayReceipt"):
                    governed[name]["adapterInfo"] = copy.deepcopy(governed["adapterInfo"])
                    governed[f"{name}Sha256"] = sha256_json(governed[name])
                write_json(candidate_path, candidate)
            failures = validate_candidate_set(
                ROOT, list(candidates.values()), "linux", "x64", ALL_RUNTIMES
            )
            self.assertEqual(failures, [])

            node = json.loads(candidates["node"].read_text(encoding="utf-8"))
            del node["governedReplay"]["adapterInfo"]["architecture"]
            write_json(candidates["node"], node)
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node",))
            self.assertTrue(
                any("'architecture' is a required property" in item for item in failures)
            )
            node["governedReplay"]["adapterInfo"]["architecture"] = None
            write_json(candidates["node"], node)
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node",))
            self.assertTrue(
                any("adapterInfo.architecture: None is not of type 'string'" in item for item in failures)
            )

    def test_windows_tuple_is_supported_and_schema_admits_win32(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidates = build_retained_bundle(Path(temporary))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "win32", "x64", ("node",))
            self.assertFalse(any("unsupported candidate tuple" in item for item in failures))
            # The retained Linux candidate is not a Windows candidate.
            self.assertTrue(any("declared tuple does not match" in item for item in failures))
            failures = validate_candidate_set(ROOT, [candidates["node"]], "win32", "arm64", ("node",))
            self.assertEqual(failures, ["unsupported candidate tuple: win32-arm64"])
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        report = json.loads(REPORTS["node"].read_text(encoding="utf-8"))
        report["tuple"]["platform"] = "win32"
        report["host"]["platform"] = "win32"
        jsonschema.validate(report, schema)

    def test_windows_rejects_the_drop_in_library_flavor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            candidates = build_retained_bundle(bundle)
            report = json.loads(candidates["node"].read_text(encoding="utf-8"))
            report["tuple"] = {"platform": "win32", "arch": "x64"}
            report["host"]["platform"] = "win32"
            report["host"]["arch"] = "x64"
            report["receipt"]["provider"]["doeLibraryPath"] = (
                "C:\\Temp\\install\\node_modules\\doe-gpu-win32-x64\\bin\\webgpu_doe.dll"
            )
            write_json(candidates["node"], report)
            failures = validate_candidate_set(ROOT, [candidates["node"]], "win32", "x64", ("node",))
            self.assertTrue(any("carry only the C runtime" in item for item in failures))

    def test_library_path_check_accepts_windows_separators(self) -> None:
        windows = "C:\\Temp\\x\\node_modules\\doe-gpu-win32-x64\\bin\\webgpu_doe_c.dll"
        self.assertTrue(library_path_in_package(windows, "doe-gpu-win32-x64"))
        self.assertFalse(library_path_in_package(windows, "doe-gpu-linux-x64"))
        self.assertFalse(
            library_path_in_package("C:\\Temp\\other\\webgpu_doe_c.dll", "doe-gpu-win32-x64")
        )
        self.assertTrue(
            library_path_in_package(
                "/tmp/x/node_modules/doe-gpu-linux-x64/bin/libwebgpu_doe_c.so", "doe-gpu-linux-x64"
            )
        )
        self.assertFalse(library_path_in_package(None, "doe-gpu-win32-x64"))

    def test_windows_platform_package_is_scoped_in_an_unscoped_directory(self) -> None:
        name, library = PLATFORM_PACKAGES[("win32", "x64")]
        self.assertEqual(name, "@clocksmith/doe-gpu-win32-x64")
        self.assertIsNone(library)
        self.assertEqual(package_directory(name), "doe-gpu-win32-x64")
        self.assertEqual(package_directory("doe-gpu-linux-x64"), "doe-gpu-linux-x64")
        scoped = "C:\\Temp\\x\\node_modules\\@clocksmith\\doe-gpu-win32-x64\\bin\\webgpu_doe_c.dll"
        self.assertTrue(library_path_in_package(scoped, name))
        unscoped = "C:\\Temp\\x\\node_modules\\doe-gpu-win32-x64\\bin\\webgpu_doe_c.dll"
        self.assertFalse(library_path_in_package(unscoped, name))

    def test_platform_pin_admits_a_newer_wrapper_pinning_a_published_platform(self) -> None:
        wrapper = json.dumps({
            "name": "doe-gpu",
            "version": "0.5.1",
            "optionalDependencies": {
                "doe-gpu-darwin-arm64": "0.5.0",
                "doe-gpu-linux-x64": "0.5.0",
                "@clocksmith/doe-gpu-win32-x64": "0.5.0",
            },
        }).encode()
        self.assertEqual(platform_pin_failures(wrapper, "doe-gpu-linux-x64", "doe-gpu-linux-x64@0.5.0"), [])
        self.assertEqual(
            platform_pin_failures(
                wrapper, "@clocksmith/doe-gpu-win32-x64", "@clocksmith/doe-gpu-win32-x64@0.5.0"
            ),
            [],
        )
        self.assertEqual(
            platform_pin_failures(wrapper, "doe-gpu-linux-x64", "doe-gpu-linux-x64@0.5.1"),
            ["platform package identity is not the wrapper's pinned doe-gpu-linux-x64@0.5.0"],
        )
        self.assertEqual(
            platform_pin_failures(wrapper, "doe-gpu-win32-x64", "doe-gpu-win32-x64@0.5.0"),
            ["wrapper does not pin doe-gpu-win32-x64"],
        )
        self.assertEqual(
            platform_pin_failures(None, "doe-gpu-linux-x64", "doe-gpu-linux-x64@0.5.0"),
            ["wrapper manifest is unavailable for the platform pin"],
        )

    def test_gate_rejects_a_platform_version_the_wrapper_does_not_pin(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bundle_root = Path(temporary)
            candidates = build_retained_bundle(bundle_root)
            node = json.loads(candidates["node"].read_text(encoding="utf-8"))
            node["packages"]["platform"]["id"] = "doe-gpu-linux-x64@0.4.9"
            write_json(candidates["node"], node)
            failures = validate_candidate_set(ROOT, [candidates["node"]], "linux", "x64", ("node",))
            self.assertIn(
                "doe-gpu-node-native-release-candidate.json: platform package identity "
                "is not the wrapper's pinned doe-gpu-linux-x64@0.5.0",
                failures,
            )

    def test_legacy_candidates_remain_schema_readable_but_lack_custody(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        for runtime, path in REPORTS.items():
            with self.subTest(runtime=runtime):
                report = json.loads(path.read_text(encoding="utf-8"))
                jsonschema.validate(report, schema)
                self.assertEqual(report["schemaVersion"], 1)
                self.assertNotIn("artifactPath", report["packages"]["wrapper"])
                self.assertNotIn("artifactPath", report["packages"]["platform"])
                self.assertNotEqual(
                    report["implementation"]["runner"]["sha256"],
                    sha256(
                        git_blob(
                            report["sourceCommit"],
                            report["implementation"]["runner"]["path"],
                        )
                    ),
                )
                self.assertEqual(report["runtime"]["host"], runtime)
                self.assertEqual(
                    report["governedReplay"]["outputSha256"],
                    EXPECTED_OUTPUT_SHA256,
                )
                self.assertTrue(all(report["governedReplay"]["matches"].values()))
                self.assertFalse(report["decision"]["registryPublicationCredit"])
                self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
                self.assertFalse(report["decision"]["performanceCredit"])
                self.assertFalse(report["decision"]["applicationPromotionCredit"])


if __name__ == "__main__":
    unittest.main()
