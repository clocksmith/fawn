# Drop-in policy C implementation style

Follow the repository native adapter subset, [contract](CONTRACT.md) and Python tooling style.

- Keep identity projection data-only. One C authority owns decisions and lazy publication.
- Use explicit fixed-width domain codes, typed spans and checked count/byte arithmetic.
- Keep cold locking separate from acquire/release publication. Never call a loader,
  diagnostic callback or native procedure while holding the policy lock.
- Keep parsing and classification explicit; preserve ordered pairs and raw numeric
  tokens instead of accidental Python coercion or recursive unbounded parsing.
- Use small readable domain helpers, not a macro framework or per-symbol recognizers.
- Do not silently retain environment/name pointers, allocate runtime registries or
  introduce public reset/testing mutation APIs.
- Preserve first failures, exact source/build identities and unrun boundaries.
