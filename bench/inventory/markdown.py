"""One small family overview and generated, complete aggregate drill-downs."""

from __future__ import annotations

from collections import Counter
from html import escape
from pathlib import Path
from typing import Any

from bench.inventory import model
from bench.inventory.storage import expand

SECTIONS = (
    "native-runtime",
    "browser-integration",
    "compiler",
    "application",
    "workspace",
)


def text(value: Any) -> str:
    return (
        escape(str("unknown" if value is None else value), quote=False)
        .replace("\n", " ")
        .replace("|", "\\|")
    )


def number(value: Any) -> str:
    if isinstance(value, float):
        return format(value, ".5g")
    return text(value)


def short(value: str | None) -> str:
    return value[:12] if value else "unknown"


def build(row: dict[str, Any], side: str) -> str:
    item = row[side]
    return f"{item['name'] or 'unknown'} {short(item['sourceCommit'])}/{short(item['binaryHash'])}"


def outcome_counts(rows: list[dict[str, Any]]) -> Counter:
    counts: Counter = Counter()
    for row in rows:
        if row["observationLevel"] == "execution":
            continue
        for metric in row["metrics"]:
            outcome = metric["outcome"]
            counts[
                outcome if outcome in ("win", "loss") else "inconclusive"
            ] += 1
    return counts


def render_current(
    inventory: dict[str, Any], *, index_href: str = "index.json"
) -> str:
    data = expand(inventory)
    lines = [
        "# Current benchmark evidence",
        "",
        "Generated from [sources.json](sources.json) by `python3 bench/cli.py inventory`.",
        "",
        f"Current product-source frontier: `{data['currentSource']}`.",
        "",
        "Each family links to its complete aggregate history. Builds and dates below summarize "
        "recorded evidence; they do not bind different machines or executions together.",
        "",
        "W/L/I counts refer to aggregate metric outcomes, not workload or campaign wins. "
        "I includes unknown, incomparable and not-classified-as-loss; none implies a win. "
        "Failed observations remain separate from metric counts. Qualification never follows from discovery.",
        "",
    ]
    by_family: dict[str, list[dict[str, Any]]] = {
        f["id"]: [] for f in data["families"]
    }
    for row in data["rows"]:
        by_family[row["familyId"]].append(row)
    selected = {s["seriesKey"]: s for s in data["current"]}
    for section in SECTIONS:
        lines += [
            f"## {section.replace('-', ' ').title()}",
            "",
            "| Family | Recorded machine / OS | Recorded builds: source/binary | Last execution / contract freeze | Metrics W/L/I; failed observations | Qualification | Current source / measurement gaps |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ]
        for family in (f for f in data["families"] if f["section"] == section):
            rows = by_family[family["id"]]
            known = sorted(
                {
                    r["machine"]["id"]
                    for r in rows
                    if not r["machine"]["id"].startswith("unknown:")
                }
            )
            unknown = sum(
                r["machine"]["id"].startswith("unknown:") for r in rows
            )
            machines = ", ".join(known[:3]) or "unbound"
            if len(known) > 3:
                machines += f" +{len(known) - 3} hosts"
            machines += f"; {unknown} unbound observations / {', '.join(family['expectedOs'])}"
            builds = sorted(
                {
                    build(r, "implementation") + " vs " + build(r, "comparator")
                    for r in rows
                }
            )
            build_text = "; ".join(builds[:2]) + (
                f"; +{len(builds) - 2} pairs" if len(builds) > 2 else ""
            )
            dates = [r["executedAt"] for r in rows if r["executedAt"]]
            frozen = [
                r["contractFrozenAt"] for r in rows if r["contractFrozenAt"]
            ]
            outcomes = outcome_counts(rows)
            failures = sum(r["executionStatus"] == "failed" for r in rows)
            qualified = sum(model.inventory_qualified(r) for r in rows)
            gaps = []
            if not any(
                selected[r["seriesKey"]]["currentSourceMeasured"] for r in rows
            ):
                gaps.append("current source unmeasured")
            else:
                gaps.append("measured subset; inspect exact builds")
            undated = sum(r["executedAt"] is None for r in rows)
            if undated:
                gaps.append(f"{undated} undated")
            if unknown:
                gaps.append("machine provenance missing")
            if any(
                e["availability"] != "retained"
                for r in rows
                for e in r["evidence"]
                if e["required"]
            ):
                gaps.append("required evidence unavailable")
            label = f"[{family['label']}](families/{family['id']}.md) ({len(rows)} observations)"
            cells = [
                label,
                machines,
                build_text,
                text(max(dates) if dates else None)
                + " / "
                + text(max(frozen) if frozen else None),
                f"{outcomes['win']}/{outcomes['loss']}/{outcomes['inconclusive']}; {failures} failed",
                f"{qualified} fully qualified observations",
                "; ".join(gaps),
            ]
            lines.append("| " + " | ".join(text(c) for c in cells) + " |")
        lines.append("")
    lines += [
        "## Inspect and reproduce",
        "",
        f"- Machine-readable [index.json]({index_href}): compact v3; `bench.inventory.storage.load_inventory` expands v2/v3.",
        "- Optional searchable dashboard: `python3 bench/cli.py inventory --dashboard-html-out bench/out/cube-inventory.html`.",
        "- Generate: `python3 bench/cli.py inventory`; verify all generated pages: `python3 bench/cli.py inventory --check`.",
        "- Public claim authority remains [claim-index.json](../claim-index.json).",
        "",
    ]
    return "\n".join(lines)


def render_families(
    inventory: dict[str, Any],
    current_path: Path,
    repo_root: Path,
    index_path: Path | None = None,
) -> dict[Path, str]:
    """Link every aggregate, its exact identity, evidence and limitations."""
    from os.path import relpath

    data = expand(inventory)
    selected = {s["seriesKey"]: s for s in data["current"]}
    outputs = {}
    for family in data["families"]:
        path = current_path.parent / "families" / (family["id"] + ".md")
        own = [r for r in data["rows"] if r["familyId"] == family["id"]]
        lines = [
            f"# {family['label']}",
            "",
            "<!-- Generated by benchmark cube; do not edit. -->",
            "",
            f"[All families](../{current_path.name}) · [Complete execution records]({relpath(index_path or current_path.parent / 'index.json', path.parent)})",
            "",
            f"{len(own)} observations; {sum(r['observationLevel'] == 'execution' for r in own)} individual executions.",
            "",
            "All aggregate and unavailable observations follow, including historical failures. "
            "Displayed numbers are rounded; exact values, sample counts and uncertainty remain in index.json. "
            "Execution, report and contract clocks remain separate.",
            "",
        ]
        for row in sorted(
            (r for r in own if r["observationLevel"] != "execution"),
            key=lambda r: (r["workloadId"], r["executedAt"] or "", r["rowId"]),
        ):
            selection = selected[row["seriesKey"]]
            machine = row["machine"]
            lines += [
                f'<a id="row-{row["rowId"]}"></a>',
                "",
                f"## {text(row['workloadId'])}",
                "",
                f"Observation: `{row['rowId']}`; execution: `{row['executionId']}`.",
                "",
                f"Machine: {text(machine['id'])}; OS: {text(machine['os'])}; backend: {text(row['backend'])}; GPU: {text(machine['gpu'])}; driver: {text(machine['driver'])}.",
                "",
                f"Build: {build(row, 'implementation')}; comparator: {build(row, 'comparator')}.",
                "",
                f"Executed: {text(row['executedAt'])}; reported: {text(row['reportedAt'])}; contract frozen: {text(row['contractFrozenAt'])}.",
                "",
                "Correctness / fallback / acceptance / qualification: "
                + " / ".join(
                    row[k]["status"]
                    for k in (
                        "correctness",
                        "fallback",
                        "acceptance",
                        "qualification",
                    )
                )
                + ".",
                "",
                f"Latest measured: `{selection['latestMeasured']}`; latest qualified: `{selection['latestQualified']}`; verdict: {text(row['verdict'])}.",
                "",
                "| Metric | Implementation / comparator | Unit | Ratio | Uncertainty | Samples | Outcome |",
                "| --- | --- | --- | --- | --- | --- | --- |",
            ]
            for metric in row["metrics"]:
                cells = [
                    metric["name"],
                    number(metric.get("implementationValue"))
                    + " / "
                    + number(metric.get("comparatorValue")),
                    metric["unit"],
                    number(metric.get("ratio")),
                    metric.get("uncertainty"),
                    metric.get("samples"),
                    metric["outcome"],
                ]
                lines.append("| " + " | ".join(text(c) for c in cells) + " |")
            lines += ["", "Evidence:", ""]
            for evidence in row["evidence"]:
                target = (
                    relpath(repo_root / evidence["path"], path.parent)
                    if evidence["path"]
                    else None
                )
                link = (
                    f"[{text(evidence['role'])}]({target})"
                    if target
                    else text(evidence["role"])
                )
                lines.append(f"- {link}: {evidence['availability']}.")
            if row["limitations"]:
                lines += [
                    "",
                    "Limitations:",
                    "",
                    *("- " + text(reason) for reason in row["limitations"]),
                ]
            if row["supersedes"]:
                lines += [
                    "",
                    "Explicit supersedes: " + ", ".join(row["supersedes"]),
                ]
            lines.append("")
        outputs[path] = "\n".join(lines)
    return outputs
