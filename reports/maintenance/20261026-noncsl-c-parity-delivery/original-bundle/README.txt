Fawn non-CSL C parity implementation — patch delivery

Publication status
------------------
Two tested commits were created locally. GitHub publication was not completed:
the final C tree connector call was cancelled before approval. No main branch
update was made. The reference-only remote commit object is unreferenced; it is
not evidence that this work is on main. This bundle is the authorized fallback.

Patch and exact identities
--------------------------
Repository: https://github.com/clocksmith/fawn
Required base commit: 3918ff8d8dafa929292fe4347cd078c36d2d8b68
Base tree: 2cee1f62e573ad48303c03775e1dbc6c45d554e3
Local reference-fix commit: 173e43022571fa1c22d48eec065c69c2e3356357
Reference tree: c84d5e15c10410093ec60d6141fedbc18fe02396
Local C-integration commit: 4170a010df689cb8fc167d3fa01ca1575fb21b81
Final exact tree: 056ff282eefdae6b5a3ef786805484c1ca44fa45

full-delivery.patch contains both logical commits. It was independently applied
to a temporary index at the required base and produced the exact final tree.
Commit IDs after git am may differ because committer metadata differs; the final
tree is the content identity to verify. The original dirty working trees were
preserved. No credentials, unrelated private files or CSL implementation changes
are included.

Apply only in a clean checkout at the required base
-------------------------------------------------
Do not reset or clean an existing dirty checkout. Use a separate clean clone if
necessary. If its main has advanced, stop and review/rebase the patch rather than
overwriting concurrent work.

  test "$(git rev-parse HEAD)" = 3918ff8d8dafa929292fe4347cd078c36d2d8b68
  test -z "$(git status --porcelain)"
  git am /absolute/path/to/full-delivery.patch
  test "$(git rev-parse HEAD^{tree})" = 056ff282eefdae6b5a3ef786805484c1ca44fa45

Stop if either precondition fails. This bundle does not push or switch a runtime
provider. Read repository AGENTS.md and component instructions before applying
or publishing further changes. Never force-push main.

Implemented scope
-----------------
- Canonical typed compiler adapters replace the duplicate DXIL source parser;
  allocating host ABI remains separate from the allocation-free core ABI.
- Bounded direct-IR HLSL graphics/numeric/texture/atomic/pointer/store handling,
  structural validation, safe native integer rewrites and collision-free temps.
- Exact TSIR text spans and target/emitter identities, iterative hint traversal,
  UTF-8 portable file access and explicit native-only fixture consumers.
- Typed shader/persistent caches, loaded-image identity, Metal library ownership,
  CLI dry-run behavior and rejected finished-encoder lifetime fixes.
- Separate retained-Zig correctness fixes for abstract/contextual inference,
  lexical constants, error propagation, shift counts and preserved call effects.

Verification and remaining limits
---------------------------------
verification-summary.json names the exact receipts. Available cited logs are
included under receipts/ with their original basenames; SHA256SUMS covers every
bundle entry except itself.

Passed: full core and host/TSIR/DXIL integration, fully instrumented UBSan and
allocation failures, header/archive incremental regression, Zig 1077 tests
(22 skipped), TSIR 30 exact semantic cases, 4152 decimal comparisons, all-owner
production/global checks and all five software-Vulkan lifetime clients.
Native DXIL differential: 24 exact matches, three documented reference admission
differences, zero unexpected differences. This is not device semantics proof.
Native Linux check inventory: 105/106 pass. Its aggregate gate still FAILS:
unchanged msaa-sampling fails on installed lavapipe, independently reproduced
without Doe. It was not waived or weakened.
Required C aggregate also still FAILS its three excluded baseline CSL orphan-test
registrations. CSL remains excluded.
Windows strict object compilation passed; new full Windows DXC, Darwin runtime,
physical-GPU/browser execution and exhaustive all-code parity are unqualified.
Shared retained native-DXIL zero-reference/no-store/nonexecuting-control backend
placeholders remain. Explicit typed budgets and unsupported caller-IR forms are
not claimed to be universal admission or hardware parity.
