# Component charter protocol

CATSCAN files are optional, searchable component maps. They describe ownership,
dependencies, important contracts and known traps; they are references for
investigation, not prerequisites to editing or approval gates for redesign.

## Authority files

| File | Owns |
| --- | --- |
| `GOALS.md` | Product goals, constraints, and exclusions |
| `docs/thesis.md` | Technical rationale and current proofs |
| `INTENT.md` | Enforceable system invariants |
| `CATSCAN.md` | One component's target, authority, scope, contracts, inherited/local invariants, and acceptance |
| `AGENTS.md` | Contributor instructions and canonical-policy discovery |
| `README.md` | Human usage and navigation |
| ADR or design document | Why a mechanism was selected |
| Tests, registries, reports, receipts | Current evidence and status |

## Using and changing a component map

Start with the requested outcome, the code and its observed behavior. Consult
[the component index](component-index.md) or a relevant charter when an ownership
or contract question needs it. Do not walk an ancestor reading chain before
coding. Parent links explain relationships; they do not add a separate pre-edit
ceremony.

A map may be outdated. Agents may redesign component boundaries within the
requested scope and update affected documentation with the implementation.
Preserve real public API, security, data and resource-lifetime constraints, or
explicitly implement and verify the intended contract change. Rewording a
charter does not fix incorrect behavior or establish performance evidence.

Add a map only where it captures independently useful architectural information.
Utility, generated, vendored, cache and fixture directories do not each need one.
Consolidate duplicate maps while preserving unique contracts and navigation.

## Shape for indexed maps

```markdown
# CATSCAN: <Component>

Parent: [<parent component>](../CATSCAN.md)

## Target
<One observable outcome.>

## Authority
- Owns <state, decisions, or contracts>.
- Does not own <adjacent authority>.

## Scope
- Includes <governed behavior or paths>.

## Contracts
- Input: <shape and canonical link>.
- Output: <shape and canonical link>.

## Invariants
- <Condition that remains true>.
- <Failure, fallback, or claim that remains explicit>.

## Acceptance
- <Observable check, command, test, or receipt>.
- Evidence: [CATSCAN gate](../bench/gates/catscan_gate.py).

## Non-goals
- <Tempting adjacent responsibility rejected here>.

## Freedom
Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
```

The repository root uses `Parent: none`. The existing index checker accepts this
shape and a 250-word limit for retained maps. Keep maps concise, but preserve
unique architectural knowledge when consolidating; move longer explanations to
a linked owner rather than deleting them to meet a word count.

## Mechanical validation

```bash
python3 bench/gates/catscan_gate.py --write-index
python3 bench/gates/catscan_gate.py
python3 -m unittest bench.tests.test_catscan_gate
```

Run the index writer and focused checker when changing indexed maps or their
links. They validate shape, nearest parents, identifiers, local links, acceptance
references, size and the generated [`component-index.md`](component-index.md).
They do not establish runtime correctness or require charter reading before
implementation. Ordinary code changes use the affected behavior's checks.

Report changed behavior, relevant evidence and remaining work. Mention a changed
boundary when it helps the next operator; no separate charter handoff ritual is
required.
