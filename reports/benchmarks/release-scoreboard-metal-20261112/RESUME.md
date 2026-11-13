# Metal release scoreboard rounds: resume note (paused 2026-11-12)

Paused on owner direction after the decode profile. Results are in
[summary.json](summary.json); raw receipts and profile data are in ignored
`bench/out/release-scoreboard/metal-20261112-*` on mac-host.

## Finished

- C stage and Doe Node addon rebuilt from fawn 83d379802d. The runner is at 6e8909e49d.
- Round r1 is the full scoreboard with Doe first. All 11 workloads are comparable, and
  `release_scoreboard_gate.py --backend metal` says the summary is admissible.
- Rows 1-3 have four timed rounds: r1 (Doe first), r2 (Dawn first), r3 (Doe first) and
  r4b (Dawn first, clean Doppler worktree at e520bf0c). r4 was refused because
  `../doppler` had another session's uncommitted edit; it carries no timing.
- Rows 4-5 were re-run once, in r1.
- Decode profile, Doe versus Dawn: per-token host API time, the submit/map timeline,
  `/usr/bin/sample`, and Metal System Trace GPU intervals.
- Candidate Doe patch, not committed:
  `bench/out/release-scoreboard/metal-20261112-profile/doe-metal-recording-use-lookup.patch`.
  It finds a recording's buffer use through the buffer's own use chain in
  `runtime/c/backends/metal/src/commands.c`. Checks run so far:
  - `make check` for the Metal backend passed (mock native calls, no GPU).
  - `make check-style` passed.
  - An interleaved A/B and a trace both used a stage built from a local detached
    commit, 4e9f6695ef.

## Pending

1. Per-kernel GPU time for decode. Doppler's `profile: true` fails on batched decode.
   The next step is Metal System Trace with the shader timeline enabled, or a
   per-dispatch timestamp harness.
2. Tests on the patch:
   - `make -C runtime/c check` on the full patched tree.
   - The doe-gpu Node integration tests (`make -C runtime/c check-js STAGE=<patched stage>`).
3. A clean interleaved decode round, scoreboard rows 1-3, with the patched stage. Use
   at least 3 rounds and alternate `--first-provider`.
4. Prefill. Its result depends on provider order, and its samples climb within a run.
   Before any prefill claim, rerun with pauses between providers and a measured
   thermal state.

## Quiet-machine check (run before every timed row)

```sh
uptime                                   # 1-min load must be < 3
ps -Ao pcpu,comm -r | head -8            # no other process >= 50% CPU (Chrome, node, chrome-headless-shell)
pgrep -fl "[c]hrome-headless-shell|[p]laywright"   # another agent's browser runs: wait
pgrep -fl "[d]oppler-generation.js|[r]un_release_scoreboard|[x]ctrace record"  # leftovers: none
git -C ../doppler status --porcelain --untracked-files=no   # must be empty, or use a clean worktree
```

## Resume commands (from `deco/fawn`)

```sh
S=<scratch dir>
make -C runtime/c package REVISION=HEAD CC=clang CXX=clang++ BUILD=$S/cbuild STAGE=$S/cstage
(cd packages/doe-gpu && node scripts/build-addon.js)
git -C ../doppler worktree add --detach $S/doppler-e520 e520bf0c   # if ../doppler is dirty
ln -s "$PWD/../doppler/node_modules" $S/doppler-e520/node_modules
export DOE_C_STAGE=$S/cstage FAWN_SCOREBOARD_DOPPLER_ROOT=$S/doppler-e520
R=qwen35-2b-decode,qwen35-2b-prefill-2k,cold-first-token
python3 bench/runners/run_release_scoreboard.py --backend metal --quiet-load 3 --first-provider doe  --rows $R --out bench/out/release-scoreboard/<run-id>-a
python3 bench/runners/run_release_scoreboard.py --backend metal --quiet-load 3 --first-provider dawn --rows $R --out bench/out/release-scoreboard/<run-id>-b
python3 bench/gates/release_scoreboard_gate.py --backend metal --summary bench/out/release-scoreboard/<full-run-id>/scoreboard-summary.json
```

To test the patch, apply it in a detached worktree. A submodule worktree needs
`git config --worktree core.worktree <dir>`. Commit it locally there and build its
stage with `REVISION=HEAD`. Then run:

```sh
make -C runtime/c check
make -C runtime/c check-js STAGE=<patched stage>
```

Set `DOE_C_STAGE=<patched stage>` for the rounds above.
