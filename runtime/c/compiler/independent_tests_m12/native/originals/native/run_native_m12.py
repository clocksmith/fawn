#!/usr/bin/env python3
"""Path-explicit M12 native gates. No candidate reads without --execute."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
OLD_REPO = "/workspace/shared/doe-c-migration-20261023"
OLD_NATIVE = "/workspace/shared/doe-c-migration-evidence-20261023/milestone11/evaluator/native"
OLD_BUILD = "/tmp/doe-m11-independent-native-build2"
OLD_OWNER = "/tmp/doe-m11-independent-native-candidate2/runtime/c/compiler"
C_FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
           "-Wvla", "-Wno-unused-const-variable", "-Wno-misleading-indentation",
           "-fno-fast-math", "-ffp-contract=off", "-frounding-math", "-fno-builtin"]
SAN = ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
WRAP = "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=aligned_alloc,--wrap=posix_memalign"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def check(path: Path, expected: str) -> None:
    if sha(path) != expected:
        raise ValueError(f"Identity mismatch: {path}")


def verify_seal(root: Path, seal: Path) -> int:
    count = 0
    for line in seal.read_text(encoding="utf-8").splitlines():
        digest, name = line.split(None, 1)
        relative = Path(name.lstrip("*"))
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Unsafe seal path: {name}")
        path = (root / relative).resolve()
        if not path.is_relative_to(root):
            raise ValueError(f"Escaping seal path: {name}")
        check(path, digest)
        count += 1
    return count


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("repo", "cases", "native", "owner", "manifest", "build-root", "out"):
        parser.add_argument("--" + name, type=Path, required=True,
                            help=f"Explicit {name} path")
    parser.add_argument("--manifest-sha256", required=True,
                        help="Expected immutable candidate source manifest digest")
    parser.add_argument("--build-receipt", type=Path,
                        help="Fresh independent build receipt for nonbuild stages")
    parser.add_argument("--stage", required=True,
                        choices=("build", "numeric", "retained", "audit", "sanitize"),
                        help="Storage-coordinated stage; never implicitly runs others")
    parser.add_argument("--execute", action="store_true",
                        help="Read candidate source and run this explicitly selected stage")
    return parser.parse_args()


class IncompleteGate(RuntimeError):
    """An external interruption left a gate unfinished, not failed semantically."""


class Runner:
    def __init__(self, out: Path, manifest_sha256: str, stage: str):
        self.out = out
        self.manifest_sha256 = manifest_sha256
        self.stage = stage
        self.rows: list[dict] = []
        self.targets: list[dict] = []

    def run(self, label: str, command: list, stdout: Path | None = None,
            env: dict | None = None) -> str:
        command = list(map(str, command))
        if "-o" in command:
            Path(command[command.index("-o") + 1]).parent.mkdir(parents=True, exist_ok=True)
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                           ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        environment.update(env or {})
        started = time.monotonic()
        log_path = self.out / (label + ".log")
        progress = {"status": "RUNNING", "stage": self.stage, "label": label,
                    "command": command, "elapsed_seconds": 0,
                    "source_manifest_sha256": self.manifest_sha256,
                    "log": str(log_path), "completed_commands": len(self.rows),
                    "wall_timeout": None,
                    "policy": "No automatic wall timeout. Parent supervises bounded work and explicit progress; interruption remains an unfinished gate."}
        save(self.out / "RUNNING.json", progress)
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                       text=True, env=environment, start_new_session=True)
            progress["pid"] = process.pid
            save(self.out / "RUNNING.json", progress)
            try:
                while True:
                    try:
                        code = process.wait(timeout=30)
                        break
                    except subprocess.TimeoutExpired:
                        # This is only a supervision tick, never a gate timeout.
                        log.flush()
                        progress["elapsed_seconds"] = time.monotonic() - started
                        progress["log_bytes"] = log_path.stat().st_size
                        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
                        progress["latest_output"] = lines[-1] if lines else None
                        save(self.out / "RUNNING.json", progress)
                        print("RUNNING " + label + " " +
                              str(round(progress["elapsed_seconds"])) + "s " +
                              str(progress["latest_output"] or "no output yet"), flush=True)
            except KeyboardInterrupt as error:
                # An explicit caller interruption authorizes stopping this child.
                process.terminate()
                process.wait()
                progress["status"] = "INCOMPLETE_INTERRUPTED"
                progress["elapsed_seconds"] = time.monotonic() - started
                save(self.out / "RUNNING.json", progress)
                save(self.out / "UNFINISHED_GATE.json", progress)
                raise IncompleteGate(label + " interrupted before completion") from error
        output = log_path.read_text(encoding="utf-8", errors="replace")
        row = {"label": label, "command": command, "returncode": code,
               "seconds": time.monotonic() - started, "stage": self.stage,
               "source_manifest_sha256": self.manifest_sha256,
               "artifact_name_note": "Retained m11 filenames are M12 source-bound outputs, not an M11-source qualification."}
        self.rows.append(row)
        save(self.out / "COMMANDS.json", self.rows)
        progress.update(elapsed_seconds=row["seconds"], returncode=code,
                        status="COMPLETE" if code == 0 else "NONZERO_EXIT")
        save(self.out / "RUNNING.json", progress)
        if code < 0 or code in (124, 137, 143) or (code and "subprocess.TimeoutExpired" in output):
            row["status"] = "INCOMPLETE_EXTERNAL_STOP"
            save(self.out / "UNFINISHED_GATE.json", row)
            raise IncompleteGate(label + " externally stopped; no compiler verdict")
        if code:
            if not (self.out / "FIRST_FAILURE.json").exists():
                save(self.out / "FIRST_FAILURE.json", row)
            raise RuntimeError(f"{label} failed; original output preserved for diagnosis")
        if stdout is not None:
            stdout.write_text(output, encoding="utf-8")
        print("PASS " + label, flush=True)
        return output


def immutable_inputs(args: argparse.Namespace) -> tuple[dict, dict, dict]:
    verify_seal(HERE, HERE / "SOURCE_SEAL.sha256")
    inputs = json.loads((HERE / "NATIVE_INPUTS.json").read_text())
    check(args.cases / "CASE_SEAL.sha256", inputs["case_seal_sha256"])
    case_files = verify_seal(args.cases, args.cases / "CASE_SEAL.sha256")
    check(args.repo / "runtime/c/compiler/CONTRACT_12.sha256",
          inputs["contract_seal_sha256"])
    check(args.native / "PORTABLE_NATIVE_INPUTS.json",
          inputs["m11_input_metadata_sha256"])
    pins = json.loads((args.native / "PORTABLE_NATIVE_INPUTS.json").read_text())
    original = pins["original_native_manifest"]
    check(args.native / original["file"], original["sha256"])
    native_files = verify_seal(args.native, args.native / original["file"])
    for name, digest in pins["repo_inputs"].items():
        check(args.repo / name, digest)
    for name, digest in pins["frozen_work_inputs"].items():
        check(args.native.parent / "frozen" / name, digest)
    check(args.native / "boundary_m11_revision1.c",
          pins["corrected_boundary_sha256"])
    identities = {"sealed_m12_files": case_files,
                  "retained_native_files": native_files,
                  "retained_repo_files": len(pins["repo_inputs"]),
                  "case_seal_sha256": inputs["case_seal_sha256"],
                  "native_source_seal_sha256": sha(HERE / "SOURCE_SEAL.sha256")}
    return inputs, pins, identities


def main() -> int:
    args = parse_args()
    for name in ("repo", "cases", "native", "owner", "manifest", "build_root", "out"):
        setattr(args, name, getattr(args, name).resolve())
    if args.build_receipt:
        args.build_receipt = args.build_receipt.resolve()
    for path in (args.repo, args.cases, args.native, args.owner, args.manifest,
                 args.build_root, args.out):
        if any(character in str(path) for character in "'\"\\\n\r"):
            raise ValueError("Code-bearing path characters unsupported")
    immutable_roots = (args.repo, args.cases, args.native, args.owner, HERE)
    for destination in (args.out, args.build_root):
        if any(destination.is_relative_to(root) or root.is_relative_to(destination)
               for root in immutable_roots):
            raise ValueError(f"Output overlaps immutable input: {destination}")
    if args.out.exists():
        raise ValueError("Every stage needs a new receipt directory")
    if args.out.is_relative_to(args.build_root) or args.build_root.is_relative_to(args.out):
        raise ValueError("Build and receipt directories must be separate")
    inputs, pins, identities = immutable_inputs(args)
    args.out.mkdir(parents=True, exist_ok=False)
    save(args.out / "IDENTITIES.json", identities)
    if not args.execute:
        save(args.out / "PLAN.json", {
            "status": "PREPARED_NOT_EXECUTED", "stage": args.stage,
            "owner": str(args.owner), "manifest": str(args.manifest),
            "manifest_sha256": args.manifest_sha256,
            "build_root": str(args.build_root),
            "candidate_read": False, "compiler_builds": 0,
            "native_case_count": len(inputs["selected_cases"]),
            "pools": inputs["pools"],
            "notes": "Run each stage separately after storage coordination. Build stage uses unchanged build_m11.py; other stages require its manifest-bound BUILD_RESULT.json. Native status/invariance is not target semantic qualification."
        })
        print("PREPARED_NOT_EXECUTED " + str(args.out))
        return 0
    runner = Runner(args.out, args.manifest_sha256, args.stage)
    try:
        check(args.manifest, args.manifest_sha256)
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("retained_m11_build",
                                                       args.native / "build_m11.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        frozen = args.repo / "runtime/c/compiler/independent_tests_m10/native/frozen_headers"
        entries = builder.verify(args.owner, args.manifest, frozen)
        identities.update(source_manifest_sha256=args.manifest_sha256,
                          candidate_source_verified=True,
                          source_entries=len(entries), milestone="M12",
                          artifact_name_note="Retained m11 artifact names bind this verified M12 source manifest, not an M11 acceptance.")
        save(args.out / "IDENTITIES.json", identities)
        for tool in ("gcc", "g++"):
            version = runner.run(tool + "-qualified-version", [tool, "-dumpfullversion"]).strip()
            if version != "14.2.0":
                raise ValueError(f"Unqualified {tool} version: {version}")
        if args.stage == "build":
            runner.run("fresh-source-build", [sys.executable, HERE / "supervised_build_m12.py",
                args.native / "build_m11.py", sha(args.native / "build_m11.py"),
                "--evaluate-candidate", "--repo", args.repo, "--owner", args.owner,
                "--manifest", args.manifest, "--expected-manifest-sha256",
                args.manifest_sha256, "--build-root", args.build_root,
                "--out", args.out / "build"])
        else:
            if args.build_receipt is None:
                raise ValueError("Nonbuild stages require --build-receipt")
            receipt = json.loads(args.build_receipt.read_text())
            if (receipt["status"] != "PASS" or receipt["manifest_sha256"] !=
                args.manifest_sha256 or Path(receipt["owner"]).resolve() != args.owner or
                Path(receipt["build_root"]).resolve() != args.build_root):
                raise ValueError("Build receipt identity/path mismatch")
            for artifact in receipt["artifacts"].values():
                check(Path(artifact["path"]), artifact["sha256"])
            tests = args.repo / "runtime/c/compiler/independent_tests_m10/native"
            allocator = tests / "allocator_m10.c"
            include = ["-I" + str(frozen), "-I" + str(args.native), "-I" + str(HERE)]
            archive = args.build_root / "libdoe-m11.a"
            shared = args.build_root / "libdoe-m11.so"
            sources = sorted(path for path in (args.owner / "src").glob("*.c")
                             if not path.name.startswith("cli"))
            if args.stage in ("numeric", "sanitize"):
                links = [("archive", [archive], ["-DWRAP_ALLOCATORS", WRAP]),
                         ("shared", [shared, "-Wl,-rpath," + str(args.build_root)], [])]
                compiler_flags = C_FLAGS
                if args.stage == "sanitize":
                    san_lib = args.build_root / "m12-sanitizer/libdoe-m12-sanitized.so"
                    runner.run("build-sanitized-library", ["gcc", *builder.STRICT, *SAN,
                        "-fPIC", "-shared", "-I" + str(args.owner / "include"),
                        "-I" + str(args.owner / "src"), *sources,
                        "-Wl,--version-script=" + str(args.owner / "src/exports.map"),
                        "-lm", "-o", san_lib])
                    links = [("sanitizer", [san_lib, "-Wl,-rpath," + str(san_lib.parent)], [])]
                    compiler_flags = [*C_FLAGS, *SAN, "-fno-pie", "-no-pie"]
                for route, link, extra in links:
                    dest = args.build_root / ("m12-" + route)
                    for harness in ("capacity_m12", "numeric_m12"):
                        runner.run("build-" + route + "-" + harness,
                            ["gcc", *compiler_flags, *include, HERE / (harness + ".c"),
                             allocator, *link, *extra, "-pthread", "-lm", "-o", dest / harness])
                    for key, row in inputs["pools"].items():
                        runner.run(route + "-pool-" + key,
                            [dest / "capacity_m12", key, args.cases / row["source"]])
                    for row in inputs["selected_cases"]:
                        target_out = args.out / "targets" / route / row["id"]
                        target_out.mkdir(parents=True, exist_ok=False)
                        command = [dest / "numeric_m12", args.cases / row["source"],
                                   row["expected_status"], row["legacy_expected_status"],
                                   row["legacy_projection_name"], target_out]
                        if row["id"] == inputs["concurrency_case"]:
                            command.append("threads")
                        try:
                            runner.run(route + "-numeric-" + row["id"], command)
                        finally:
                            # Preserve emitted outputs even when a later native check fails.
                            for path in sorted(target_out.glob("api*.spv")):
                                runner.targets.append({"case_id": row["id"],
                                    "source": str(args.cases / row["source"]),
                                    "source_sha256": row["source_sha256"],
                                    "source_manifest_sha256": args.manifest_sha256,
                                    "route": route, "api": int(path.stem[3:]),
                                    "abi": {1: "fixed", 2: "v2", 3: "job", 4: "typed"}[int(path.stem[3:])],
                                    "path": str(path), "sha256": sha(path),
                                    "scenario_source": str(args.cases / "cases.json"),
                                    "scenario_key": row["id"],
                                    "target_execution_status": "PENDING_INDEPENDENT_MODEL"})
                            save(args.out / "TARGET_MANIFEST.json", {"targets": runner.targets,
                                "case_seal_sha256": inputs["case_seal_sha256"],
                                "source_manifest_sha256": args.manifest_sha256})
            if args.stage in ("retained", "sanitize"):
                mapping = {OLD_REPO: str(args.repo), OLD_NATIVE: str(args.native),
                           OLD_BUILD: str(args.build_root), OLD_OWNER: str(args.owner)}
                if args.stage == "sanitize":
                    mapping[OLD_BUILD + "/consumers-all/libdoe-m11-sanitized.so"] = str(san_lib)
                    mapping[OLD_BUILD + "/consumers-all"] = str(san_lib.parent)
                pattern = re.compile("|".join(re.escape(key) for key in
                                             sorted(mapping, key=len, reverse=True)))
                def relocate(value: str) -> str:
                    return pattern.sub(lambda match: mapping[match.group()], value)
                for name in ("candidate2-consumers/commands.json",
                             "candidate2-boundary-revision1/commands.json",
                             "candidate2-public-hardcap/commands.json"):
                    for row in json.loads((args.native / name).read_text()):
                        label = row["label"]
                        is_sanitizer = "san-" in label or "sanitiz" in label
                        if "prebuilt" in label or label == "sanitized-library":
                            continue
                        if name.startswith("candidate2-consumers") and "boundary_m11" in label:
                            continue  # Exact revision1 replaces only the known bad supplement.
                        if is_sanitizer != (args.stage == "sanitize"):
                            continue
                        command = [relocate(value) for value in row["command"]]
                        if command[0].endswith(("python3", "python")):
                            command[0] = sys.executable
                        environment = {"PYTHONPATH": str(args.repo / "runtime/c/compiler/independent_tests_m7")} if label.startswith("retained-") else None
                        runner.run(Path(name).parent.name + "-" + label,
                                   command, env=environment)
                # Private work controls use all current source units in sanitizer mode.
                private_flags = [*C_FLAGS, *SAN, "-fno-pie", "-no-pie"] if args.stage == "sanitize" else C_FLAGS
                for source_name in ("private_work_m11.c", "private_adapters_m11.c"):
                    name = Path(source_name).stem
                    binary = args.build_root / ("m12-" + args.stage) / name
                    link = sources if args.stage == "sanitize" else [archive]
                    runner.run("build-" + name, ["gcc", *private_flags, *include,
                        "-I" + str(args.owner / "src"), args.native / source_name,
                        allocator, *link, "-DWRAP_ALLOCATORS", WRAP,
                        "-Wl,--wrap=uniformity_analyze", "-pthread", "-lm", "-o", binary])
                    arguments = [args.native.parent / "frozen" / key for key in
                                 sorted(pins["frozen_work_inputs"])] if "private_work" in name else []
                    runner.run("run-" + name, [binary, *arguments])
            if args.stage == "audit":
                includes = ["-I" + str(frozen), "-I" + str(args.owner / "src")]
                layout_binary = args.build_root / "m12-layout"
                runner.run("build-layout", ["gcc", *C_FLAGS, *includes,
                    args.native / "layout_m11.c", archive, "-lm", "-o", layout_binary])
                runner.run("run-layout", [layout_binary], args.out / "candidate2-layout.log")
                runner.run("complete-stack-parser", [sys.executable,
                    args.repo / "runtime/c/compiler/independent_tests_m10/postdisclosure/analyze_stack.py",
                    "--root", args.build_root])
                original = args.native / "audit_layout_stack_m11.py"
                text = original.read_text()
                changes = {OLD_BUILD: str(args.build_root), OLD_OWNER: str(args.owner),
                    "b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af": args.manifest_sha256}
                for old, new in changes.items():
                    if text.count(old) != 1:
                        raise ValueError("Unexpected retained audit literal count")
                    text = text.replace(old, new)
                relocated = args.out / "audit_layout_stack_m12.py"
                relocated.write_text(text, encoding="utf-8")
                save(args.out / "AUDIT_RELOCATION.json", {"original": str(original),
                    "original_sha256": sha(original), "relocated_sha256": sha(relocated),
                    "literal_changes": changes, "expectations_changed": False})
                runner.run("complete-stack-layout-path-audit", [sys.executable, relocated])
                id_source = args.repo / "runtime/c/compiler/independent_tests_m10/postdisclosure/id_boundary.c"
                id_binary = args.build_root / "m12-id-boundary"
                runner.run("build-target-id-boundary", ["gcc", *C_FLAGS, *includes,
                    "-ffunction-sections", "-fdata-sections", id_source,
                    "-Wl,--gc-sections", "-lm", "-o", id_binary])
                runner.run("target-id-boundary", [id_binary])
                runner.run("readonly-global-audit", [sys.executable, HERE / "audit_binary_m12.py",
                    "--build-root", args.build_root, "--build-receipt", args.build_receipt,
                    "--out", args.out / "GLOBAL_STATE.json"])
        if builder.verify(args.owner, args.manifest, frozen) != entries:
            raise ValueError("Source changed during selected stage")
        immutable_inputs(args)
        save(args.out / "RESULT.json", {"status": "PASS_SELECTED_NATIVE_STAGE",
            "stage": args.stage, "source_manifest_sha256": args.manifest_sha256,
            "commands": len(runner.rows), "target_artifacts": len(runner.targets),
            "target_semantics": "Separate parent-owned independent consumption required",
            "scope": "Qualified Linux x86-64 GCC14 native checks only; no physical GPU, TSan or whole migration claim"})
        return 0
    except Exception as error:
        status = "INCOMPLETE_GATE" if isinstance(error, IncompleteGate) else "FAIL_NATIVE_GATE"
        save(args.out / "RESULT.json", {"status": status, "stage": args.stage,
            "error": str(error), "source_manifest_sha256": args.manifest_sha256,
            "original_failure_preserved": True})
        raise


if __name__ == "__main__":
    raise SystemExit(main())
