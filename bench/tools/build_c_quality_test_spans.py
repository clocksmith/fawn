#!/usr/bin/env python3
"""Build or refresh docs/c-migration/test-span-audit.json for c_quality_target_audit.py."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_ROOT = REPO_ROOT / "bench"
for _path_entry in (str(REPO_ROOT), str(BENCH_ROOT)):
    if _path_entry not in sys.path:
        sys.path.insert(0, _path_entry)

from bench.gates import c_quality_target_audit as audit  # noqa: E402

ZIG_AST_HELPER_SOURCE = r"""const std = @import("std");

fn declNameToken(tree: *const std.zig.Ast, decl: std.zig.Ast.Node.Index) ?std.zig.Ast.TokenIndex {
    const tags = tree.nodes.items(.tag);
    const token_tags = tree.tokens.items(.tag);
    switch (tags[@intFromEnum(decl)]) {
        .fn_decl, .fn_proto_simple, .fn_proto_multi, .fn_proto, .fn_proto_one => {
            var buf: [1]std.zig.Ast.Node.Index = undefined;
            if (tree.fullFnProto(&buf, decl)) |proto| {
                return proto.name_token;
            }
            return null;
        },
        .simple_var_decl, .aligned_var_decl, .local_var_decl, .global_var_decl => {
            if (tree.fullVarDecl(decl)) |vd| {
                const name_tok = vd.ast.mut_token + 1;
                if (name_tok < token_tags.len and token_tags[name_tok] == .identifier) {
                    return name_tok;
                }
            }
            return null;
        },
        else => return null,
    }
}

fn isPubOrExported(tree: *const std.zig.Ast, first_tok: std.zig.Ast.TokenIndex, main_tok: std.zig.Ast.TokenIndex) bool {
    const token_tags = tree.tokens.items(.tag);
    var t = first_tok;
    while (t <= main_tok and t < token_tags.len) : (t += 1) {
        switch (token_tags[t]) {
            .keyword_pub, .keyword_export, .keyword_extern => return true,
            else => {},
        }
    }
    return false;
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);
    if (args.len != 2) return error.InvalidArgs;

    const raw = try std.fs.cwd().readFileAllocOptions(allocator, args[1], 64 * 1024 * 1024, null, .@"1", 0);
    defer allocator.free(raw);

    var tree = try std.zig.Ast.parse(allocator, raw, .zig);
    defer tree.deinit(allocator);

    var stdout_buf: [8192]u8 = undefined;
    var stdout_writer = std.fs.File.stdout().writer(&stdout_buf);
    const out = &stdout_writer.interface;

    const tags = tree.nodes.items(.tag);
    const main_tokens = tree.nodes.items(.main_token);
    const token_starts = tree.tokens.items(.start);
    const token_tags = tree.tokens.items(.tag);

    try out.print("{{\"parse_errors\":{d},\"test_spans\":[", .{tree.errors.len});
    var first = true;
    for (0..tags.len) |idx| {
        if (tags[idx] == .test_decl) {
            const node: std.zig.Ast.Node.Index = @enumFromInt(idx);
            const first_tok = tree.firstToken(node);
            const last_tok = tree.lastToken(node);
            const start_byte: usize = token_starts[first_tok];
            const end_byte: usize = @as(usize, token_starts[last_tok]) + tree.tokenSlice(last_tok).len;
            if (!first) try out.writeAll(",");
            first = false;
            try out.print("[{d},{d}]", .{ start_byte, end_byte });
        }
    }
    try out.writeAll("],\"root_decls\":[");
    first = true;
    for (tree.rootDecls()) |decl| {
        const idx = @intFromEnum(decl);
        if (tags[idx] == .test_decl) continue;
        const first_tok = tree.firstToken(decl);
        const last_tok = tree.lastToken(decl);
        var end_tok = last_tok;
        if (end_tok + 1 < token_tags.len and token_tags[end_tok + 1] == .semicolon) {
            end_tok += 1;
        }
        const start_byte: usize = token_starts[first_tok];
        const end_byte: usize = @as(usize, token_starts[end_tok]) + tree.tokenSlice(end_tok).len;
        const pub_or_exp = isPubOrExported(&tree, first_tok, main_tokens[idx]);
        const name_opt = declNameToken(&tree, decl);
        if (!first) try out.writeAll(",");
        first = false;
        if (name_opt) |nt| {
            const name = tree.tokenSlice(nt);
            try out.print("{{\"start\":{d},\"end\":{d},\"pub\":{},\"name\":", .{
                start_byte,
                end_byte,
                pub_or_exp,
            });
            try std.json.Stringify.value(name, .{}, out);
            try out.print(",\"name_byte\":{d}}}", .{token_starts[nt]});
        } else {
            try out.print("{{\"start\":{d},\"end\":{d},\"pub\":true,\"name\":null,\"name_byte\":null}}", .{
                start_byte,
                end_byte,
            });
        }
    }
    try out.writeAll("],\"identifiers\":[");
    first = true;
    for (0..token_tags.len) |tok_i| {
        if (token_tags[tok_i] == .identifier) {
            if (tok_i > 0 and token_tags[tok_i - 1] == .period) continue;
            const tok: std.zig.Ast.TokenIndex = @intCast(tok_i);
            const s = tree.tokenSlice(tok);
            const b: usize = token_starts[tok];
            if (!first) try out.writeAll(",");
            first = false;
            try out.print("[{d},", .{b});
            try std.json.Stringify.value(s, .{}, out);
            try out.writeAll("]");
        }
    }
    try out.writeAll("]}\n");
    try out.flush();
}
"""

# Reviewed preprocessor conditional-test line ranges (1-based inclusive) in C production files.
REVIEWED_C_TEST_LINE_SPANS: dict[str, list[tuple[int, int, str]]] = {
    "runtime/c/futures/src/futures.c": [
        (11, 16, "futures_test_limit_override_guard"),
    ],
    "runtime/c/futures/src/internal.h": [
        (6, 11, "futures_darwin_test_profile_guard"),
    ],
    "runtime/c/instance/src/internal.h": [
        (12, 12, "instance_test_reference_limit_branch"),
    ],
    "runtime/c/labels/src/store.c": [
        (12, 14, "labels_test_capacity_guard"),
        (69, 73, "labels_test_capacity_override"),
    ],
    "runtime/c/platform/include/doe_platform_posix.h": [
        (10, 12, "platform_darwin_relative_test_decl"),
    ],
}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="", help="Repository root.")
    parser.add_argument(
        "--out",
        default="docs/c-migration/test-span-audit.json",
        help="Output JSON path relative to --root.",
    )
    parser.add_argument(
        "--zig-bin",
        default="",
        help="Zig 0.15.2 executable used when rebuilding baseline AST spans.",
    )
    parser.add_argument(
        "--refresh-candidates-only",
        action="store_true",
        help="Reuse existing baseline_files in --out and recompute candidate_files without Zig.",
    )
    return parser.parse_args(argv)


def expand_span_to_enclosing_lines(
    blob: bytes,
    start_byte: int,
    end_byte: int,
    line_intervals: list[tuple[int, int]],
) -> tuple[int, int]:
    """Expand token span to full physical lines + attached leading // comments when unshared."""
    s_exp, e_exp = start_byte, end_byte
    start_line_idx: int | None = None
    for idx, (ls, le) in enumerate(line_intervals):
        if ls <= start_byte < le:
            if blob[ls:start_byte].strip(b" \t") == b"":
                s_exp = ls
                start_line_idx = idx
            break
    for ls, le in line_intervals:
        if ls < end_byte <= le:
            tail = blob[end_byte:le].rstrip(b"\r\n").lstrip(b" \t")
            if tail == b"" or tail.startswith(b"//"):
                e_exp = le
            break
    if start_line_idx is not None:
        idx = start_line_idx - 1
        while idx >= 0:
            ls, le = line_intervals[idx]
            stripped = blob[ls:le].strip(b" \t\r\n")
            if stripped.startswith(b"//") and not stripped.startswith(b"//!"):
                s_exp = ls
                idx -= 1
            else:
                break
    return s_exp, e_exp


def extract_baseline_zig_spans(root: Path, zig_bin: str) -> tuple[dict[str, Any], str]:
    zig_ver = subprocess.check_output([zig_bin, "version"], text=True).strip()
    inv = json.loads((root / "docs/c-migration/zig-inventory.json").read_text(encoding="utf-8"))
    prod = [f for f in inv["files"] if f["role"] == "production"]

    proc = subprocess.Popen(
        ["git", "-C", str(root), "cat-file", "--batch"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    )
    req = "".join(f["git_blob"] + "\n" for f in prod).encode("ascii")
    out_bytes, _ = proc.communicate(req)
    pos = 0
    blobs: dict[str, bytes] = {}
    for entry in prod:
        nl = out_bytes.index(b"\n", pos)
        size = int(out_bytes[pos:nl].decode("ascii").split()[2])
        data = out_bytes[nl + 1 : nl + 1 + size]
        pos = nl + 1 + size + 1
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise RuntimeError(f"baseline sha256 mismatch: {entry['path']}")
        blobs[entry["path"]] = data

    baseline_records: dict[str, Any] = {}
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        helper_src = td_path / "zig_ast_helper.zig"
        helper_bin = td_path / "zig_ast_helper"
        blob_path = td_path / "unit.zig"
        helper_src.write_text(ZIG_AST_HELPER_SOURCE, encoding="utf-8")
        subprocess.run(
            [zig_bin, "build-exe", str(helper_src), "-O", "ReleaseFast", f"-femit-bin={helper_bin}"],
            check=True,
        )

        for entry in sorted(prod, key=lambda item: item["path"]):
            rel_path = entry["path"]
            blob = blobs[rel_path]
            blob_path.write_bytes(blob)
            parsed = json.loads(subprocess.check_output([str(helper_bin), str(blob_path)], text=True))
            if parsed["parse_errors"] != 0:
                raise RuntimeError(f"Zig AST parse error in {rel_path}")
            test_spans = parsed["test_spans"]
            root_decls = parsed["root_decls"]
            idents = parsed["identifiers"]

            exclusively_test_decls: list[dict[str, Any]] = []
            if test_spans:
                priv = [d for d in root_decls if not d["pub"] and d["name"]]
                prod_decls = [d for d in root_decls if d["pub"] or not d["name"]]
                changed = True
                while changed:
                    changed = False
                    prod_ranges = [(d["start"], d["end"]) for d in prod_decls]
                    used_in_prod = {
                        name
                        for b_pos, name in idents
                        if any(rs <= b_pos < re_ for rs, re_ in prod_ranges)
                    }
                    still_priv = []
                    for d in priv:
                        if d["name"] in used_in_prod:
                            prod_decls.append(d)
                            changed = True
                        else:
                            still_priv.append(d)
                    priv = still_priv

                for d in priv:
                    hits = sum(
                        1
                        for b_pos, name in idents
                        if name == d["name"]
                        and b_pos != d["name_byte"]
                        and (
                            any(ts <= b_pos < te for ts, te in test_spans)
                            or any(od["start"] <= b_pos < od["end"] for od in priv if od is not d)
                        )
                    )
                    if hits > 0:
                        exclusively_test_decls.append(d)

            line_intervals = audit.physical_line_byte_intervals(blob)
            spans: list[dict[str, Any]] = []
            for s_byte, e_byte in test_spans:
                se, ee = expand_span_to_enclosing_lines(blob, s_byte, e_byte, line_intervals)
                spans.append(
                    {
                        "start_byte": se,
                        "end_byte": ee,
                        "purpose": "exclusively_test_only",
                        "suite": "zig_ast_test_decl",
                        "reviewed": True,
                    }
                )
            for d in exclusively_test_decls:
                se, ee = expand_span_to_enclosing_lines(blob, d["start"], d["end"], line_intervals)
                spans.append(
                    {
                        "start_byte": se,
                        "end_byte": ee,
                        "purpose": "exclusively_test_only",
                        "suite": f"zig_ast_test_support:{d['name']}",
                        "reviewed": True,
                    }
                )
            spans.sort(key=lambda item: (item["start_byte"], item["end_byte"]))
            impl_lines, excl_lines, errs = audit.evaluate_file_test_spans(blob, spans)
            if errs:
                raise RuntimeError(f"{rel_path}: {errs}")
            record: dict[str, Any] = {
                "sha256": entry["sha256"],
                "physical_lines": entry["line_count"],
                "implementation_lines": impl_lines,
                "excluded_test_only_lines": excl_lines,
                "spans": spans,
            }
            baseline_records[rel_path] = record
    return baseline_records, zig_ver


def extract_candidate_c_spans(root: Path) -> dict[str, Any]:
    tracked = audit.git_tracked_paths(
        root,
        ("runtime/c", "runtime/zig", "runtime/bridge", "packages/doe-gpu", "browser/chromium"),
    )
    charged = sorted(p for p in tracked if audit.is_charged_production_path(p))
    test_macro_pat = re.compile(rb"#\s*if[^\n]*(?:TEST|MOCK|INJECT|FAULT)", re.I)

    records: dict[str, Any] = {}
    for rel_path in charged:
        data = (root / rel_path).read_bytes()
        digest = audit.sha256_bytes(data)
        total_lines = audit.count_physical_lines(data)
        line_intervals = audit.physical_line_byte_intervals(data)
        spans: list[dict[str, Any]] = []

        if rel_path in REVIEWED_C_TEST_LINE_SPANS:
            for start_line, end_line, suite_name in REVIEWED_C_TEST_LINE_SPANS[rel_path]:
                s_byte = line_intervals[start_line - 1][0]
                e_byte = line_intervals[end_line - 1][1]
                spans.append(
                    {
                        "start_byte": s_byte,
                        "end_byte": e_byte,
                        "purpose": "exclusively_test_only",
                        "suite": suite_name,
                        "reviewed": True,
                    }
                )
        elif test_macro_pat.search(data):
            raise RuntimeError(
                f"unreviewed conditional test macro in charged candidate file: {rel_path}"
            )

        impl_lines, excl_lines, errs = audit.evaluate_file_test_spans(data, spans)
        if errs:
            raise RuntimeError(f"{rel_path}: {errs}")
        records[rel_path] = {
            "sha256": digest,
            "physical_lines": total_lines,
            "implementation_lines": impl_lines,
            "excluded_test_only_lines": excl_lines,
            "spans": spans,
        }
    return records


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve() if args.root else REPO_ROOT
    out_path = root / args.out

    if args.refresh_candidates_only:
        existing = json.loads(out_path.read_text(encoding="utf-8"))
        baseline_files = existing["baseline_files"]
        zig_ver = existing["method"]["zig_ast_version"]
    else:
        zig_bin = args.zig_bin or shutil.which("zig") or ""
        if not zig_bin:
            raise SystemExit("error: --zig-bin or zig on PATH required for baseline AST audit")
        baseline_files, zig_ver = extract_baseline_zig_spans(root, zig_bin)

    candidate_files = extract_candidate_c_spans(root)

    base_total = sum(v["physical_lines"] for v in baseline_files.values())
    base_impl = sum(v["implementation_lines"] for v in baseline_files.values())
    base_excl = sum(v["excluded_test_only_lines"] for v in baseline_files.values())

    cand_total = sum(v["physical_lines"] for v in candidate_files.values())
    cand_impl = sum(v["implementation_lines"] for v in candidate_files.values())
    cand_excl = sum(v["excluded_test_only_lines"] for v in candidate_files.values())

    payload = {
        "schema_version": 1,
        "owner": "docs/c-migration/QUALITY_TARGET.md",
        "baseline_inventory_sha256": "9bb2524f32d724a0d74b9a3a76c28c39719aef59bd5cb6b089fa6dc87bbd613d",
        "baseline_source_revision": "665475907494d2095a760596218b4c2d363cb436",
        "method": {
            "generator": "bench/tools/build_c_quality_test_spans.py",
            "zig_ast_version": zig_ver,
            "baseline_parser": "std.zig.Ast (.test_decl plus private rootDecls fixpoint reachability and attached // line comments)",
            "candidate_parser": "exact byte-line preprocessor conditional-test audit across all charged C/glue paths",
            "human_readability_and_span_signoff": False,
        },
        "baseline_all_628_files_reviewed": True,
        "baseline_summary": {
            "files": len(baseline_files),
            "physical_lines": base_total,
            "implementation_only_lines": base_impl,
            "excluded_test_only_lines": base_excl,
        },
        "baseline_implementation_only_lines": base_impl,
        "candidate_summary": {
            "files": len(candidate_files),
            "physical_lines": cand_total,
            "implementation_only_lines": cand_impl,
            "excluded_test_only_lines": cand_excl,
        },
        "baseline_files": baseline_files,
        "candidate_files": candidate_files,
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        f"test-span-audit written: baseline={base_impl}/{base_total} (excl {base_excl}), "
        f"candidate={cand_impl}/{cand_total} (excl {cand_excl}) -> {out_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
