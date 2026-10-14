# Doe Python style guide

This guide is the Python style contract for `bench/`, `pipeline/`, `scripts/`,
and Python helper tooling under `runtime/zig/tools/`.

## Core principles

- Fail fast with descriptive errors. No silent fallbacks.
- Keep modules focused (see File size).
- Fairness before speed: benchmark code must prove both sides did equivalent
  work before presenting a faster result as evidence.
- For package/runtime benchmarks, compare the effective readback path from
  trace telemetry, not the requested mode; native map-read-copy-unmap versus
  `mapAsync` needs the declared comparison class. Fixed-path experiments need
  path parity; application replacement needs equivalent output/completion
  semantics and complete timing boundaries. Follow
  `docs/performance-strategy.md`; existing frozen gates keep their meaning.
- Read raw timing before percent deltas: compute the speed ratio from
  `baselineStatsMs` and `comparisonStatsMs` before calling a result a win.
- Treat speedups at or above `reliability.suspiciousSpeedupRatio` as suspected
  path or measurement differences until the report proves otherwise: check
  cache/prepared-session state, skipped work, one-sided
  setup/upload/submit/readback costs, hidden fallback, stale artifacts, and
  timing scope.

## File naming

- Modules: `snake_case.py` (e.g. `bench_utils.py`, `config_validation.py`).
- Test files: `test_<module>.py` (e.g. `test_mine_quirks.py`,
  `test_config_schemas.py`).
- Shell-invoked entry points: prefer `kebab-case.py` (e.g. `check-health.py`).
  Imported utility modules may stay `snake_case.py` even when also runnable.

## Imports

- Use `from __future__ import annotations` in new files, kept at the top.
- Standard library first, then third-party, then local.
- Use absolute imports. No relative imports.
- Package modules and canonical front doors must not mutate `sys.path`.
  `bench` and `pipeline` are importable packages; invoke their canonical
  entrypoints with `python3 -m ...`.
  `config/python-import-boundaries.json` isolates legacy directly executed
  scripts, and the blocking schema gate enforces protected paths.
- Prefer `pathlib.Path` over `os.path` in new code; some legacy files still use
  `os.path`.

## Naming

- Functions: `snake_case` (`load_json`, `build_match_object`,
  `detect_repo_root`)
- Classes: `PascalCase` (`ToggleHit`, `SchemaTargetEntry`)
- Variables and fields: `snake_case` (`toggle_candidates`, `scanned_files`)
- Constants: `UPPER_SNAKE_CASE` (`HASH_SEED`, `ZIG_MAX_LINES`,
  `DEFAULT_ALLOWED_SUFFIXES`)
- Private functions: leading underscore (`_infer_schema_path`,
  `_format_path`)
- Boolean variables: `is_`/`has_`/`should_` prefix when the bare name is
  ambiguous

## Type hints

- Annotate all function parameters and return types.
- Use `str | None`, not `Optional[str]`, and built-in generics
  (`list[str]`, `dict[str, Any]`), not `List[str]`.
- Use `Any` sparingly; prefer narrow types when the shape is known.

```python
def load_validated_config(
    config_path: str | Path,
    schema_path: str | Path | None = None,
) -> dict[str, Any]:
```

## Docstrings

- NumPy-style with triple double quotes.
- Module docstrings for non-trivial modules; function docstrings for public
  functions, omitted for trivial helpers.
- Use `Parameters`, `Returns`, `Raises` sections for complex signatures.

```python
def load_json(path: Path) -> Any:
    """Load a JSON or JSONL file and return its parsed content.

    For ``.json`` files the return type mirrors the top-level JSON value.
    For ``.jsonl`` files a list of parsed objects is returned.
    """
```

## Error handling

- Raise specific exception types (`FileNotFoundError`, `ValueError`,
  `json.JSONDecodeError`). Avoid bare `Exception` in new code; a few legacy
  aggregation paths keep it to continue processing.
- Include actionable context: what was expected, what was received, which
  file.
- Use `from exc` for exception chaining.

## Entry points

- Use `def main() -> int:` returning 0/1.
- Prefer `if __name__ == "__main__": raise SystemExit(main())` for new
  scripts.
- Parse arguments in a separate `parse_args()`; small legacy helpers may
  inline CLI setup.

## CLI arguments

- Use `argparse` with structured argument definitions.
- Hyphenated names: `--source-root`, `--allow-suffix`.
- `help` text for every argument.
- `required=True` for mandatory arguments; `choices=[]` for enumerated values.

## JSON handling

- Always specify `encoding="utf-8"` in file operations.
- Canonical output: `json.dumps(value, indent=2, sort_keys=True) + "\n"`.
- JSONL: one object per line, skip blank lines on read.

## Data classes

- Prefer `@dataclass` over raw dicts for typed internal state;
  `@dataclass(frozen=True)` for immutable records.

## Formatting

- Line length: prefer 80 characters in new and touched files; the tree is not
  uniformly enforced.
- 4-space indentation (PEP 8 default).
- No formatter is mandated; touched code follows PEP 8 and the line length.

## Testing

- Workloads are the promotion and evidence category; focused module tests stay
  valid for a smaller first-failure boundary or unique oracle sensitivity.
- Remove a focused behavioral test as a workload duplicate only after the
  duplicate-removal gate proves equivalent sensitivity. Tests that only pin
  config literals or restate another file need no gate; remove the duplication.
- Use `unittest.TestCase` class-based tests named
  `test_<what_is_being_tested>` (descriptive, snake_case).
- Dynamic test generation with `setattr` for schema fuzz tests.
- Run the touched module (`python3 -m unittest bench.tests.test_<module>`).
  `python3 -m unittest discover bench.tests` is the full suite, not a default
  gate.

```python
class TestSchemaCompliance(unittest.TestCase):
    def test_reference_toggle_schema(self):
        record = self._make_candidate()
        errors = _validate_quirk_record(record)
        self.assertEqual(errors, [], f"schema errors: {errors}")
```

## Comments

- Comments explain why, not what.
- Section separators: `# ---` or `# ===` for major sections in large files.
- No TODO/FIXME inline. Track follow-ups in the status log (`docs/status.md`, with dated entries in the current `docs/status/*.md` shard).

## File size

- 1200 lines max per Python file; when exceeded, add a tracked sharding
  follow-up in the current status shard.
- Split by cohesive functionality, not arbitrary line count.
