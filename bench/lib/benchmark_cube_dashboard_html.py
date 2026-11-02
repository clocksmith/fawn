"""HTML rendering helpers for benchmark cube dashboards."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from bench.lib import visual_report_theme


STATUS_ORDER = {
    "unsupported": 0,
    "unimplemented": 1,
    "diagnostic": 2,
    "comparable": 3,
    "claimable": 4,
}

safe_float = visual_report_theme.safe_float
format_delta = visual_report_theme.format_delta


def status_badge(status: str, *, kind: str = "comparison") -> str:
    return visual_report_theme.badge(
        status,
        tone=visual_report_theme.status_tone(status, kind=kind),
    )


def cell_sort_key(cell: dict[str, Any]) -> tuple[int, str, str]:
    return (
        STATUS_ORDER.get(str(cell.get("status", "unimplemented")), -1),
        str(cell.get("workloadSet", "")),
        str(cell.get("hostProfile", "")),
    )


def metric_class(value: float | None) -> str:
    tone = visual_report_theme.delta_tone(value)
    if tone == "good":
        return "metric-positive"
    if tone == "bad":
        return "metric-negative"
    return "metric-neutral"


def heatmap_fill(status: str) -> tuple[str, str]:
    tone = visual_report_theme.status_tone(status)
    if tone == "good":
        return ("#cfeede", "#0d6c3d")
    if tone == "warn":
        return ("#f2e5bf", "#8e6110")
    if tone == "bad":
        return ("#f2d3cc", "#9c3b2d")
    return ("#e4ebf2", "#516273")


def build_heatmap_svg(
    *,
    surface: dict[str, Any],
    cells: list[dict[str, Any]],
    host_profiles: dict[str, Any],
    workload_sets: dict[str, Any],
) -> str:
    width_per_col = 172
    height_per_row = 56
    left_margin = 206
    top_margin = 36
    width = left_margin + len(surface["expectedHostProfiles"]) * width_per_col + 16
    height = top_margin + len(surface["workloadSets"]) * height_per_row + 16
    cell_map = {
        (cell["hostProfile"], cell["workloadSet"]): cell
        for cell in cells
    }

    parts = [
        f'<svg viewBox="0 0 {width} {height}" width="{width}" height="{height}" role="img" '
        f'aria-label="{surface["displayName"]} benchmark cube heatmap">'
    ]
    for col_idx, host_id in enumerate(surface["expectedHostProfiles"]):
        x = left_margin + col_idx * width_per_col + 10
        label = host_profiles[host_id]["displayName"]
        parts.append(
            f"<text x='{x + 72}' y='20' text-anchor='middle' font-size='12' fill='#31435a' font-weight='700'>"
            f"{label}</text>"
        )

    for row_idx, workload_id in enumerate(surface["workloadSets"]):
        y = top_margin + row_idx * height_per_row
        parts.append(
            f"<text x='12' y='{y + 32}' font-size='12' fill='#31435a' font-weight='700'>"
            f"{workload_sets[workload_id]['displayName']}</text>"
        )
        for col_idx, host_id in enumerate(surface["expectedHostProfiles"]):
            x = left_margin + col_idx * width_per_col
            cell = cell_map[(host_id, workload_id)]
            fill, ink = heatmap_fill(str(cell.get("status", "unimplemented")))
            delta = format_delta(safe_float(cell.get("deltaP50MedianPercent")))
            label = str(cell.get("status", "unimplemented"))
            parts.append(
                f"<rect x='{x}' y='{y}' width='156' height='42' rx='14' ry='14' "
                f"fill='{fill}' stroke='rgba(28,53,77,0.14)'/>"
            )
            parts.append(
                f"<text x='{x + 78}' y='{y + 18}' text-anchor='middle' font-size='11' "
                f"font-weight='700' fill='{ink}'>{label}</text>"
            )
            parts.append(
                f"<text x='{x + 78}' y='{y + 33}' text-anchor='middle' font-size='11' fill='#31435a'>{delta}</text>"
            )
    parts.append("</svg>")
    return "".join(parts)


def build_surface_table_rows(
    *,
    surface: dict[str, Any],
    cells: list[dict[str, Any]],
    host_profiles: dict[str, Any],
    workload_sets: dict[str, Any],
) -> str:
    rows: list[str] = []
    for cell in sorted(cells, key=cell_sort_key, reverse=True):
        host_label = host_profiles[cell["hostProfile"]]["displayName"]
        workload_label = workload_sets[cell["workloadSet"]]["displayName"]
        conformance = str(cell.get("sourceConformance", ""))
        if not conformance and cell.get("reportCount", 0) == 0:
            conformance = "-"
        status_detail = str(cell.get("statusDetail", "")) or "-"
        delta_value = safe_float(cell.get("deltaP50MedianPercent"))
        rows.append(
            "<tr>"
            f"<td>{workload_label}</td>"
            f"<td>{host_label}</td>"
            f"<td>{status_badge(str(cell.get('status', 'unimplemented')))}</td>"
            f"<td>{status_badge(str(cell.get('comparisonStatus', cell.get('status', 'unimplemented'))))}</td>"
            f"<td>{status_badge(str(cell.get('claimStatus', cell.get('status', 'unimplemented'))), kind='claim')}</td>"
            f"<td>{str(cell.get('reportCount', 0))}</td>"
            f"<td>{str(cell.get('rowCount', 0))}</td>"
            f"<td class='{metric_class(delta_value)}'>{format_delta(delta_value)}</td>"
            f"<td><code>{str(cell.get('latestGeneratedAt', '-'))}</code></td>"
            f"<td><code>{conformance}</code></td>"
            f"<td>{status_detail}</td>"
            "</tr>"
        )
    if rows:
        return "".join(rows)
    return "<tr><td colspan='11'>No cells.</td></tr>"


def build_source_rows(summary: dict[str, Any]) -> str:
    rows: list[str] = []
    source_counts = summary.get("sourceCounts", {})
    labels = {
        "backendReports": "Native backend",
        "nodeReports": "Node package",
        "bunReports": "Bun package",
    }
    for key in ("backendReports", "nodeReports", "bunReports"):
        payload = source_counts.get(key, {})
        rows.append(
            "<tr>"
            f"<td>{labels[key]}</td>"
            f"<td>{str(payload.get('discovered', 0))}</td>"
            f"<td>{str(payload.get('included', 0))}</td>"
            f"<td>{str(payload.get('canonicalIncluded', 0))}</td>"
            f"<td>{str(payload.get('legacyIncluded', 0))}</td>"
            f"<td>{str(payload.get('skipped', 0))}</td>"
            "</tr>"
        )
    return "".join(rows)


def artifact_link(
    *,
    dashboard_path: str | Path | None,
    target_path: str,
) -> str:
    if not target_path:
        return "-"
    if dashboard_path:
        href = visual_report_theme.relative_href(dashboard_path, target_path)
        return f"<a href='{href}'><code>{Path(target_path).name}</code></a>"
    return f"<code>{target_path}</code>"


def build_dashboard_html(
    summary: dict[str, Any],
    policy: dict[str, Any],
    *,
    dashboard_path: str | Path | None = None,
) -> str:
    if summary.get("kind") == "benchmark-cube-inventory":
        return render_inventory_html(summary, dashboard_path)
    host_profiles = policy["hostProfiles"]
    workload_sets = policy["workloadSets"]
    raw_policy = policy["raw"]
    cells = summary.get("cells", [])
    generated_at = str(summary.get("generatedAt", ""))
    artifacts = summary.get("artifacts", {})
    source_rows = build_source_rows(summary)

    cards_html = "".join(
        [
            visual_report_theme.stat_card(
                "Rows",
                str(summary.get("rowCount", 0)),
                detail="Normalized report rows represented in the cube.",
            ),
            visual_report_theme.stat_card(
                "Cells",
                str(len(cells)),
                detail="Host and workload set intersections covered by policy.",
            ),
            visual_report_theme.stat_card(
                "Claimable cells",
                str(summary.get("statusCounts", {}).get("claimable", 0)),
                tone="good",
                detail="Cells whose current best evidence is claimable.",
            ),
            visual_report_theme.stat_card(
                "Comparable cells",
                str(summary.get("statusCounts", {}).get("comparable", 0)),
                tone="good",
                detail="Cells that are currently comparable but not yet claimable.",
            ),
            visual_report_theme.stat_card(
                "Diagnostic cells",
                str(summary.get("statusCounts", {}).get("diagnostic", 0)),
                tone="warn",
                detail="Cells blocked by instrumentation or symmetry caveats.",
            ),
            visual_report_theme.stat_card(
                "Unimplemented cells",
                str(summary.get("statusCounts", {}).get("unimplemented", 0)),
                tone="neutral",
                detail="Policy cells that still lack a conforming report.",
            ),
        ]
    )

    artifact_links = (
        "<div class='tile-grid'>"
        "<article class='tile'>"
        "<h3>Primary outputs</h3>"
        "<p>The dashboard is only one view over the cube. The linked machine artifacts carry the same run in normalized row form and markdown matrix form.</p>"
        "<div class='link-list'>"
        f"<div>rows: {artifact_link(dashboard_path=dashboard_path, target_path=str(artifacts.get('rowsPath', '')))}</div>"
        f"<div>matrix: {artifact_link(dashboard_path=dashboard_path, target_path=str(artifacts.get('matrixMarkdownPath', '')))}</div>"
        f"<div>dashboard: {artifact_link(dashboard_path=dashboard_path, target_path=str(artifacts.get('dashboardHtmlPath', '')))}</div>"
        "</div>"
        "</article>"
        "<article class='tile'>"
        "<h3>What the cube answers</h3>"
        "<p>Which surface is implemented, which host and workload combinations are comparable, and where Doe is already claimably faster than its incumbent comparison side.</p>"
        "</article>"
        "</div>"
    )

    surface_sections: list[str] = []
    for surface in raw_policy["surfaces"]:
        surface_cells = [
            cell for cell in cells if cell.get("surface") == surface["id"]
        ]
        heatmap = build_heatmap_svg(
            surface=surface,
            cells=surface_cells,
            host_profiles=host_profiles,
            workload_sets=workload_sets,
        )
        notes_html = "".join(f"<li>{note}</li>" for note in surface["notes"])
        table_rows = build_surface_table_rows(
            surface=surface,
            cells=surface_cells,
            host_profiles=host_profiles,
            workload_sets=workload_sets,
        )
        surface_sections.append(
            "<section class='section'>"
            "<div class='section-head'>"
            "<div>"
            f"<h2>{surface['displayName']}</h2>"
            f"<div class='section-copy'>Maturity <code>{surface['maturity']}</code>, primary support <code>{surface['primarySupport']}</code>. The heatmap compresses claimable, comparable, diagnostic, and missing cells into one surface view.</div>"
            "</div>"
            "</div>"
            f"<div class='table-shell' style='padding:12px; overflow:auto; background:rgba(255,255,255,0.6);'>{heatmap}</div>"
            "<div class='fine-print'><strong>Surface notes.</strong><ul>"
            f"{notes_html}"
            "</ul></div>"
            "<div class='table-shell'><table><thead><tr>"
            "<th>Workload set</th><th>Host</th><th>Status</th><th>Comparison</th><th>Claim</th><th>Reports</th><th>Rows</th><th>Median p50 delta</th><th>Latest UTC</th><th>Conformance</th><th>Detail</th>"
            "</tr></thead><tbody>"
            f"{table_rows}"
            "</tbody></table></div>"
            "</section>"
        )

    body_html = (
        "<section class='section'>"
        "<div class='section-head'>"
        "<div>"
        "<h2>Cube summary</h2>"
        "<div class='section-copy'>One policy-governed matrix for native backend, Node package, and Bun package evidence. This is the right artifact when you want a fast answer to coverage and claim maturity by surface.</div>"
        "</div>"
        "</div>"
        f"<div class='stat-grid'>{cards_html}</div>"
        "<div class='fine-print'>Positive deltas mean Doe is faster because cube rows preserve the compare report convention that Doe is the baseline side.</div>"
        "</section>"
        "<section class='section'>"
        "<div class='section-head'>"
        "<div>"
        "<h2>Cube artifacts and source intake</h2>"
        "<div class='section-copy'>These links point at the canonical artifacts for this cube run, followed by source-discovery counts for native, Node, and Bun compare receipts.</div>"
        "</div>"
        "</div>"
        f"{artifact_links}"
        "<div class='table-shell' style='margin-top:14px;'><table><thead><tr>"
        "<th>Source</th><th>Discovered</th><th>Included</th><th>Canonical</th><th>Legacy</th><th>Skipped</th>"
        "</tr></thead><tbody>"
        f"{source_rows}"
        "</tbody></table></div>"
        "</section>"
        + "".join(surface_sections)
    )

    meta_html = " | ".join(
        [
            f"generated: <code>{generated_at}</code>",
            f"policy cells: <code>{len(cells)}</code>",
            f"dashboard artifact: {artifact_link(dashboard_path=dashboard_path, target_path=str(artifacts.get('dashboardHtmlPath', '')))}",
        ]
    )

    return visual_report_theme.render_page(
        title="Doe benchmark cube",
        eyebrow="Doe benchmark cube",
        headline="Cross-surface coverage",
        intro=(
            "A policy-normalized matrix over native, Node, and Bun evidence so backend "
            "coverage and claim maturity can be read at a glance."
        ),
        meta_html=meta_html,
        body_html=body_html,
    )


def render_matrix_markdown(summary: dict[str, Any], policy: dict[str, Any]) -> str:
    host_profiles = policy["hostProfiles"]
    workload_sets = policy["workloadSets"]
    cells = {
        (
            cell["surface"],
            cell["hostProfile"],
            cell["workloadSet"],
        ): cell
        for cell in summary["cells"]
    }
    lines = [
        "# Benchmark Cube",
        "",
        f"Generated: `{summary['generatedAt']}`",
        "",
        f"Rows: `{summary['rowCount']}`",
        "",
    ]

    for surface in policy["raw"]["surfaces"]:
        lines.append(f"## {surface['displayName']}")
        lines.append("")
        lines.append(
            f"Maturity: `{surface['maturity']}`. Primary support: `{surface['primarySupport']}`."
        )
        lines.append("")
        header = ["Workload Set", *[host_profiles[item]["displayName"] for item in surface["expectedHostProfiles"]]]
        lines.append("| " + " | ".join(header) + " |")
        lines.append("| " + " | ".join("---" for _ in header) + " |")
        for workload_set_id in surface["workloadSets"]:
            row = [workload_sets[workload_set_id]["displayName"]]
            for host_profile_id in surface["expectedHostProfiles"]:
                cell = cells[(surface["id"], host_profile_id, workload_set_id)]
                if cell["reportCount"] == 0:
                    detail = str(cell.get("statusDetail") or "").strip()
                    row.append(f"{cell['status']} ({detail})" if detail else cell["status"])
                else:
                    row.append(f"{cell['status']} ({cell['rowCount']} rows)")
            lines.append("| " + " | ".join(row) + " |")
        lines.append("")
        for note in surface["notes"]:
            lines.append(f"- {note}")
        lines.append("")

    return "\n".join(lines)



INVENTORY_SECTIONS = ("native-runtime", "browser-integration", "compiler", "application", "workspace")


def render_inventory_markdown(inventory: dict[str, Any]) -> str:
    """Project aggregates and shared selections; link complete individual observations."""
    from collections import Counter
    from os.path import relpath
    def text(value: Any) -> str:
        return str(value if value is not None else "unknown").replace("\n", " ")
    rows = {row["rowId"]: row for row in inventory["rows"]}
    selections = {s["seriesKey"]: s for s in inventory["current"]}
    lines = ["# Current benchmark evidence", "",
             "Generated by the existing benchmark cube. Register → generate → inspect CURRENT.", "",
             "Current product-source frontier: `" + inventory["currentSource"] + "`. "
             "Dates come from receipts; report and freeze dates never imply execution. "
             "Latest qualified remains separately scoped; no claim-index authority is granted.", "",
             "## Measurement queue", "",
             "| Family | Expected OS / recorded OS | Observations | Current source | Missing/stale evidence |",
             "| --- | --- | ---: | --- | --- |"]
    for family in inventory["families"]:
        own = [rows[i] for i in family["observations"]]
        measured = any(selections[r["seriesKey"]]["currentSourceMeasured"] for r in own)
        gaps = Counter(reason for r in own for reason in r["limitations"])
        lines.append("| " + text(family["label"]) + " | " + text(", ".join(family["expectedOs"])) +
                     " / " + text(", ".join(sorted({r["machine"]["os"] or "unknown" for r in own}))) +
                     " | " + str(len(own)) + " | " +
                     ("measured subset; inspect exact builds" if measured else "current source unmeasured") +
                     " | " + text("; ".join(sorted(gaps)[:5]) or "none reported") + " |")
    for section in INVENTORY_SECTIONS:
        lines += ["", "## " + section.replace("-", " ").title(), "",
                  "All aggregate observations appear below. Individual executions, including failures, "
                  "remain in [index.json](index.json) with the same current-selection fields. "
                  "An undated result cannot establish latest measured or latest qualified.", "",
                  "| Workload / execution | Machine / OS / backend / GPU / driver | Measured build / comparator | Execution / report / freeze | Metric outcomes | Correctness / fallback / acceptance / qualification | Current selection | Evidence and limitations |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        for row in sorted((r for r in rows.values() if r["section"] == section and r["observationLevel"] != "execution"),
                          key=lambda r: (r["familyId"], r["workloadId"], r["executedAt"] or "", r["rowId"])):
            machine = row["machine"]
            selection = selections[row["seriesKey"]]
            tags = []
            if selection["latestMeasured"] == row["rowId"]:
                tags.append("latestMeasured")
            if selection["latestQualified"] == row["rowId"]:
                tags.append("latestQualified")
            if row["rowId"] in selection["undated"]:
                tags.append("undated")
            if not selection["currentSourceMeasured"]:
                tags.append("current source unmeasured")
            def build(side: str) -> str:
                item = row[side]
                return (text(item["name"]) + " source=" + text(item["sourceCommit"][:12] if item["sourceCommit"] else None) +
                        " binary=" + text(item["binaryHash"][:12] if item["binaryHash"] else None) + " components=" +
                        text({k: v[:12] for k, v in item["componentHashes"].items()}))
            def metric_text(m: dict[str, Any]) -> str:
                result = text(m["name"]) + "=" + text(m.get("implementationValue"))
                if m.get("comparatorValue") is not None:
                    result += "/" + text(m["comparatorValue"])
                result += " " + text(m["unit"])
                if m.get("ratio") is not None:
                    result += " ratio=" + text(m["ratio"])
                if m.get("uncertainty"):
                    result += " CI=" + text(m["uncertainty"]["bounds"])
                samples = m.get("samples", {})
                if samples.get("implementation") is not None:
                    result += " n=" + text(samples["implementation"])
                return result + " " + text(m["outcome"])
            metrics = "; ".join(metric_text(m) for m in row["metrics"]) or "No measurement; " + row["verdict"]
            evidence = "; ".join("[" + text(e["role"]) + "](" +
                relpath(e["path"], "reports/benchmarks") + ") " + e["availability"]
                if e["path"] else e["role"] + " missing" for e in row["evidence"]
                if e["role"] in ("report", "contract", "execution"))
            extra = len([e for e in row["evidence"] if e["role"] not in ("report", "contract", "execution")])
            if extra:
                evidence += "; " + str(extra) + " supplemental references in [index.json](index.json)"
            cells = [row["workloadId"] + " / " + row["executionId"],
                     " / ".join(text(machine[k]) for k in ("id", "os")) + " / " + text(row["backend"]) +
                     " / " + text(machine["gpu"]) + " / " + text(machine["driver"]),
                     build("implementation") + " / " + build("comparator"),
                     " / ".join(text(row[k]) for k in ("executedAt", "reportedAt", "contractFrozenAt")),
                     metrics,
                     " / ".join(row[k]["status"] for k in ("correctness", "fallback", "acceptance", "qualification")),
                     "; ".join(tags) or "historical observation",
                     evidence + "; " + "; ".join(reason for reason in row["limitations"]
                        if " is unknown" not in reason and "noreceipt" not in reason and not reason.endswith(" is unknown.")) +
                     ("; supersedes=" + ",".join(row["supersedes"]) if row["supersedes"] else "")]
            lines.append("| " + " | ".join(text(c).replace("|", "\\|") for c in cells) + " |")
    return "\n".join(lines) + "\n"


def render_inventory_html(inventory: dict[str, Any], dashboard_path: Path) -> str:
    """Render shared current fields; do not select winners in presentation code."""
    import html
    from os.path import relpath
    by_id = {r["rowId"]: r for r in inventory["rows"]}
    cells = []
    for selected in inventory["current"]:
        def link(row_id: str | None) -> str:
            if row_id is None:
                return "unknown"
            row = by_id[row_id]
            values = "; ".join(m["name"] + ": " + m["outcome"] for m in row["metrics"])
            return html.escape(row["workloadId"] + " / " + str(row["executedAt"]) + " / " +
                str(row["implementation"]["binaryHash"]) + " / " + values)
        observed = by_id[selected["observations"][0]]
        label = observed["section"] + " / " + observed["workloadId"] + " / " + observed["machine"]["id"]
        cells.append("<tr><td>" + html.escape(label) + "</td><td>" +
                     link(selected["latestMeasured"]) + "</td><td>" + link(selected["latestQualified"]) +
                     "</td><td>" + ("measured" if selected["currentSourceMeasured"] else "current source unmeasured") +
                     "</td><td>" + str(len(selected["undated"])) + " undated observations</td></tr>")
    return ("<!doctype html><html><head><meta charset='utf-8'><title>Benchmark cube inventory</title></head>"
            "<body><h1>Benchmark cube inventory</h1><p>Exact evidence and complete observations: "
            "<a href='" + html.escape(relpath("reports/benchmarks/CURRENT.md", dashboard_path.parent), quote=True) +
            "'>CURRENT.md</a></p><table><thead><tr><th>Series</th><th>Latest measured</th>"
            "<th>Latest qualified</th><th>Current source</th><th>Undated</th></tr></thead><tbody>" +
            "".join(cells) + "</tbody></table></body></html>\n")
