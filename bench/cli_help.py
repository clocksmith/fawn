"""Task-oriented help for the unchanged benchmark command surface."""

from __future__ import annotations

import argparse

COMMAND_GROUPS = (
    (
        "Run Workloads & Campaigns",
        (
            "workload",
            "run",
            "run-config",
            "phase-cohort",
            "browser",
            "c-vs-zig",
            "c-vs-zig-package",
            "graphics",
            "generation",
            "generation-memory",
            "onnx-measure",
            "external",
        ),
    ),
    (
        "Analyze & Compare Receipts",
        (
            "compare",
            "browser-analyze",
            "catalog-report",
            "projection-cost",
            "compile-compare",
        ),
    ),
    ("Inventory & Evidence Inspection", ("inventory", "list")),
    ("Qualification & Program Gates", ("claim", "program")),
)


class WorkflowHelpFormatter(argparse.HelpFormatter):
    def _format_action(self, action: argparse.Action) -> str:
        if not isinstance(action, argparse._SubParsersAction):
            return super()._format_action(action)
        commands = {item.dest: item for item in action._get_subactions()}
        lines = []
        shown = set()
        for label, names in COMMAND_GROUPS:
            lines += [label + ":"]
            for name in names:
                if name in commands:
                    lines += [f"  {name:<20} {commands[name].help}"]
                    shown.add(name)
            lines.append("")
        for name in sorted(set(commands) - shown):
            lines += [f"  {name:<20} {commands[name].help}"]
        return "\n".join(lines) + "\n"
