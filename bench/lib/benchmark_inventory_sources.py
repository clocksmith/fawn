"""Compatibility imports; adapters live in bench.inventory.sources."""
from bench.inventory.sources import (
    ADAPTERS,
    file_hash,
    read_payload,
    Source,
    evidence_reference,
    make_source,
    metric,
    phase_unit,
    assign_machine,
    bind_phase,
    native_catalog,
    phase_cohort,
    browser_launches,
    browser_fresh_launch,
    workspace,
    compare_report,
    application,
    normalize_source,
)
# Retained for callers instrumenting the evidence-reader process boundary.
from bench.inventory.sources import subprocess
