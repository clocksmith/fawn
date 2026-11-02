"""Filtered, paginated presentation over the cube's existing selections."""

from __future__ import annotations

from collections import Counter
from html import escape
import json
from os.path import relpath
from pathlib import Path
from typing import Any

from bench.inventory import model
from bench.inventory.storage import expand
from bench.lib import visual_report_theme as theme


def os_filter(recorded: str | None) -> str:
    """Group recorded OS names for navigation without rewriting evidence."""
    value = (recorded or "").lower()
    if value.startswith(("darwin", "macos", "mac os")):
        return "darwin"
    if value.startswith(("win32", "windows")):
        return "win32"
    if "linux" in value or value.startswith(("ubuntu", "debian", "fedora")):
        return "linux"
    return "unknown"


def render_dashboard(
    inventory: dict[str, Any], path: Path, *, current_path: Path | None = None
) -> str:
    data = expand(inventory)
    selections = {s["seriesKey"]: s for s in data["current"]}
    families = {f["id"]: f for f in data["families"]}
    root = Path(__file__).resolve().parents[2]
    current_path = current_path or root / "reports/benchmarks/CURRENT.md"
    rows = []
    for row in data["rows"]:
        selected = selections[row["seriesKey"]]
        rows.append(
            {
                "id": row["rowId"],
                "level": row["observationLevel"],
                "section": row["section"],
                "family": row["familyId"],
                "familyLabel": families[row["familyId"]]["label"],
                "workload": row["workloadId"],
                "os": os_filter(row["machine"]["os"]),
                "recordedOs": row["machine"]["os"] or "unknown",
                "machine": row["machine"]["id"],
                "backend": row["backend"],
                "build": " / ".join(
                    str(row["implementation"][k] or "unknown")
                    for k in ("sourceCommit", "binaryHash")
                ),
                "comparator": " / ".join(
                    str(row["comparator"][k] or "unknown")
                    for k in ("name", "sourceCommit", "binaryHash")
                ),
                "date": row["executedAt"],
                "freeze": row["contractFrozenAt"],
                "metrics": [
                    {
                        k: m.get(k)
                        for k in (
                            "name",
                            "outcome",
                            "unit",
                            "implementationValue",
                            "comparatorValue",
                            "ratio",
                        )
                    }
                    for m in row["metrics"]
                ],
                "qualified": model.inventory_qualified(row),
                "status": row["executionStatus"],
                "verdict": row["verdict"],
                "currentSourceMeasured": bool(
                    selected["currentSourceMeasured"]
                ),
                "latestMeasured": selected["latestMeasured"],
                "latestQualified": selected["latestQualified"],
                "history": relpath(
                    current_path.parent
                    / "families"
                    / (row["familyId"] + ".md"),
                    path.parent,
                ),
            }
        )
        if row["observationLevel"] != "execution":
            rows[-1]["history"] += "#row-" + row["rowId"]
    family_order = {
        family["id"]: index for index, family in enumerate(data["families"])
    }
    rows.sort(
        key=lambda row: (
            family_order[row["family"]],
            row["workload"],
            row["date"] or "",
            row["id"],
        )
    )
    payload = json.dumps(rows, separators=(",", ":"), allow_nan=False).replace(
        "<", "\\u003c"
    )
    controls = []
    for key, label in (
        ("os", "OS"),
        ("machine", "Machine"),
        ("section", "Section"),
        ("family", "Family"),
    ):
        values = sorted(
            {
                ("unknown" if r[key].startswith("unknown:") else r[key])
                for r in rows
            }
        )
        if key == "os":
            values = ["darwin", "linux", "win32", "unknown"]
        options = "".join(
            f'<option value="{escape(v, quote=True)}">{escape(families[v]["label"] if key == "family" else v)}</option>'
            for v in values
        )
        controls.append(
            f'<label>{label}<select id="{key}"><option value="">All</option>{options}</select></label>'
        )
    for key, label in (
        ("workload", "Workload"),
        ("metric", "Metric"),
        ("build", "Source or binary hash"),
    ):
        controls.append(
            f'<label>{label}<input id="{key}" type="search" placeholder="Filter {label.lower()}" /></label>'
        )
    current_href = escape(relpath(current_path, path.parent), quote=True)
    body = (
        """<style>
      .hero h1 {max-width:none;font-size:clamp(30px,3vw,44px)}
      .hero .intro {max-width:110ch}
      .filters {display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}
      label {display:flex;flex-direction:column;gap:5px;font-weight:600}
      input,select,button {font:inherit;padding:9px;border:1px solid var(--line-strong);border-radius:7px;background:var(--panel-strong);color:var(--ink)}
      .inventory-table {overflow:auto;max-height:70vh;border:1px solid var(--line);border-radius:9px}
      .inventory-table th {position:sticky;top:0;z-index:1;background:var(--panel-strong)} td {max-width:420px;overflow-wrap:anywhere} .pager {display:flex;align-items:center;gap:12px;margin:16px 0}
      .metric-list {margin:0;padding-left:16px}.muted {color:var(--muted)}
    </style><section class="section"><h2>Find recorded evidence</h2><div class="filters">"""
        + "".join(controls)
        + """
      <label>Observation level<select id="level"><option value="aggregate">Summary records</option><option value="execution">Individual executions</option><option value="all">All observations</option></select></label>
      <label>Outcome<select id="status"><option value="">All statuses</option><option value="failed">Failed observations</option><option value="qualified">Fully qualified</option></select></label>
      </div><p id="count" role="status" aria-live="polite"></p><button id="reset">Reset filters</button>
      <div class="pager"><button id="previous">Previous</button><span id="page"></span><button id="next">Next</button></div>
      <div class="inventory-table"><table><thead><tr><th>Workload / family</th><th>Machine / OS / backend</th><th>Measured build / comparator</th><th>Execution / freeze</th><th>Metric outcomes</th><th>Selection / qualification</th></tr></thead><tbody id="results"></tbody></table></div>
      </section><script type="application/json" id="inventory-data">"""
        + payload
        + "</script><script>"
        + (Path(__file__).with_name("dashboard.js").read_text(encoding="utf-8"))
        + "</script>"
    )
    levels = Counter(r["level"] for r in rows)
    cards = theme.stat_card("Families", str(len(families))) + theme.stat_card(
        "Observations", f"{len(rows):,}"
    )
    cards += theme.stat_card(
        "Individual executions",
        f"{levels['execution']:,}",
        detail="Opt in using the observation-level filter",
    )
    return theme.render_page(
        title="Benchmark evidence",
        eyebrow="Fawn / Doe",
        headline="What was measured, on what?",
        intro="Filter recorded observations. Unknown identities remain distinct; diagnostic results do not grant qualification. "
        "A loss-only classification does not make every remaining result a win.",
        meta_html=f'<a href="{current_href}">16-family overview and measurement queue</a>',
        hero_extra_html='<div class="stat-grid">' + cards + "</div>",
        body_html=body,
    )
