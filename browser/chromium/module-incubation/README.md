# Track B (modules) prototype lane

Status: archived. Track B internal-module proposals are retired under
[Chromium integration acceptance](../acceptance.md#archived-work). These
prototypes and their `fawn_*` module ids remain as historical fixtures; they are
not an active lane and do not name Fawn's durable workspace service.

This directory contains executable Track B (modules) incubation artifacts for the browser
integration layer.

Scope:

1. schema-backed request fixtures for Track B (modules),
2. config-driven deterministic prototype execution,
3. gate output proving fixture validation and deterministic replay.

These prototypes are nursery-only. They do not change core runtime behavior and
they are not promotion evidence by themselves.

Files:

1. `policy.json`
   - config-as-code thresholds and allowed operation sets.
2. `schemas/*.schema.json`
   - request/result contract schemas per Track B (modules).
3. `fixtures/*.request.json`
   - deterministic prototype requests for each Track B (modules).

Scripts:

1. `scripts/module_prototype.py`
   - validate one request, execute deterministic prototype logic, emit result JSON.
2. `scripts/check_module_prototypes.py`
   - run all Track B (modules) fixtures, rerun for determinism, emit a gate report.
