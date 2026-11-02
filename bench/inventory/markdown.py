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
                outcome if outcome in ("win", "loss", "inconclusive") else "unknown"
            ] += 1
    return counts


def machine_scope(row: dict[str, Any]) -> tuple[str, str, str, str]:
    machine = row["machine"]
    identity = machine["id"]
    # The unbound presentation bucket is history, never an inferred shared host.
    # Logical machine and series identities remain separate in model.py.
    return ("unbound" if identity.startswith("unknown:") else identity,
            machine["os"] or "unknown", row["backend"] or "unknown",
            model.stable_hash([machine["osVersion"], machine["gpu"], machine["driver"]]))


def overview_scopes(rows: list[dict[str, Any]], latest_ids: set[str]) -> list[list[dict[str, Any]]]:
    """Show newest campaigns per observed machine, never rank undated evidence."""
    groups: dict[tuple[str, str, str, str], list[dict[str, Any]]] = {}
    aggregates = [row for row in rows if row["observationLevel"] != "execution"] or rows
    for row in aggregates:
        groups.setdefault(machine_scope(row), []).append(row)
    scopes = []
    for group in groups.values():
        if machine_scope(group[0])[0] == "unbound":
            scopes.append(group)
            continue
        dated = [row for row in group if row["rowId"] in latest_ids and row["executedAt"]]
        if not dated:
            scopes.append(group)
            continue
        newest = max(model.parse_utc_iso(row["executedAt"]) for row in dated)
        campaigns = {row["campaignId"] for row in dated if model.parse_utc_iso(row["executedAt"]) == newest}
        scopes.append([row for row in dated if row["campaignId"] in campaigns])
    return sorted(scopes, key=lambda scope: (
        any(row["executedAt"] for row in scope),
        max((model.parse_utc_iso(row["executedAt"]) for row in scope if row["executedAt"]), default=None),
        machine_scope(scope[0])), reverse=True)


def adapter_label(value: Any) -> str:
    if not isinstance(value, dict):
        return text(value)
    return str(value.get("name") or value.get("device") or value.get("description")
               or "/".join(str(value[k]) for k in ("vendor", "architecture") if value.get(k)) or "unknown")


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
        "Newest dated campaigns are shown separately for each observed machine/adapter. "
        "Unbound evidence is grouped as history, never treated as one host or ranked across unknown machines. "
        "Undated evidence is never ranked by report or contract dates. Family links retain the complete history.",
        "",
        "Outcome counts refer to metrics in the shown scope, not whole-workload wins. "
        "Inconclusive and unknown are separate; not-classified-as-loss is unknown, never a win. "
        "Failures remain visible and older qualified results are reported separately.",
        "",
    ]
    by_family: dict[str, list[dict[str, Any]]] = {
        f["id"]: [] for f in data["families"]
    }
    for row in data["rows"]:
        by_family[row["familyId"]].append(row)
    selected = {s["seriesKey"]: s for s in data["current"]}
    latest_ids = {s["latestMeasured"] for s in data["current"] if s["latestMeasured"]}
    qualified_ids = {s["latestQualified"] for s in data["current"] if s["latestQualified"]}
    for section in SECTIONS:
        lines += [
            f"## {section.replace('-', ' ').title()}",
            "",
            "| Family / shown scope | Machine / platform | Measured builds: source/binary | Execution / contract freeze | Metric wins/losses/inconclusive/unknown; failures | Latest measured / latest qualified | Current source / measurement gaps |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ]
        scoped_families = [(family, scope) for family in data["families"] if family["section"] == section
                           for scope in (overview_scopes(by_family[family["id"]], latest_ids) or [[]])]
        for family, rows in scoped_families:
            all_rows = by_family[family["id"]]
            if not rows:
                label = f"[{family['label']}](families/{family['id']}.md)"
                lines.append(f"| {label} | unknown | unknown | unknown | unknown | none | current source unmeasured; no registered observations |")
                continue
            known = sorted(
                {
                    r["machine"]["id"]
                    for r in rows
                    if not r["machine"]["id"].startswith("unknown:")
                }
            )
            machines = ", ".join(known[:3]) or "unbound history (not one host)"
            if len(known) > 3:
                machines += f" +{len(known) - 3} hosts"
            oses = sorted({r["machine"]["os"] for r in rows if r["machine"]["os"]})
            backends = sorted({r["backend"] for r in rows if r["backend"]})
            machines += " / " + (", ".join(oses) or "OS unknown") + " / " + (", ".join(backends) or "backend unknown")
            machines += "; GPU " + adapter_label(rows[0]["machine"]["gpu"]) + "; driver " + adapter_label(rows[0]["machine"]["driver"])
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
            if not known:
                build_text = "unbound history: " + build_text
            elif not dates:
                build_text = "undated history: " + build_text
            frozen = [
                r["contractFrozenAt"] for r in rows if r["contractFrozenAt"]
            ]
            outcomes = outcome_counts(rows)
            campaigns = {r["campaignId"] for r in rows}
            scope_records = [r for r in all_rows if r["campaignId"] in campaigns
                             and machine_scope(r) == machine_scope(rows[0])]
            failures = sum(r["executionStatus"] in ("failed", "partially-failed") for r in scope_records)
            qualified = sum(model.inventory_qualified(r) for r in rows)
            old_qualified = [r for r in all_rows if r["rowId"] in qualified_ids
                             and machine_scope(r) == machine_scope(rows[0])]
            qualification = f"shown: {qualified} qualified; latest qualified: none"
            if old_qualified and known:
                prior = max(old_qualified, key=lambda r: model.parse_utc_iso(r["executedAt"]))
                qualification = f"shown: {qualified} qualified; latest qualified: {prior['workloadId']} at {prior['executedAt']} ({short(prior['implementation']['sourceCommit'])})"
            if not known:
                qualification = f"history: {qualified} qualified; host-specific current result unknown"
            gaps = []
            if not any(
                selected[r["seriesKey"]]["currentSourceMeasured"] for r in rows
            ):
                gaps.append("current source unmeasured")
            else:
                gaps.append("measured subset; inspect exact builds")
            undated = sum(r["executedAt"] is None for r in all_rows)
            if undated:
                gaps.append(f"history: {undated} undated")
            unbound_history = sum(r["machine"]["id"].startswith("unknown:") for r in all_rows)
            if unbound_history:
                gaps.append(f"history: {unbound_history} unbound records")
            shown_ids = {r["rowId"] for r in scope_records}
            other_failures = sum(r["rowId"] not in shown_ids and r["executionStatus"] in ("failed", "partially-failed")
                                 for r in all_rows)
            if other_failures:
                gaps.append(f"history: {other_failures} other failed records")
            if any(
                e["availability"] != "retained"
                for r in rows
                for e in r["evidence"]
                if e["required"]
            ):
                gaps.append("required evidence unavailable")
            workloads = sorted({r["workloadId"] for r in rows})
            scope_label = ", ".join(workloads[:2]) + (f"; +{len(workloads) - 2} workloads" if len(workloads) > 2 else "")
            label = f"[{family['label']}](families/{family['id']}.md) — {scope_label} ({len(rows)} shown / {len(all_rows)} total)"
            cells = [
                label,
                machines,
                build_text,
                text(max(dates) if dates else None)
                + " / "
                + text(max(frozen) if frozen else None),
                f"{outcomes['win']}/{outcomes['loss']}/{outcomes['inconclusive']}/{outcomes['unknown']}; {failures} failed records",
                qualification,
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
