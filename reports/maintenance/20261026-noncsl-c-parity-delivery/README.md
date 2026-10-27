# Non-CSL C parity delivery integration

Consumer: source-bound acceptance of the user-supplied Downloads delivery and
the platform rebuilds required before the C-versus-Zig benchmark campaign.
The [original bundle](original-bundle/README.txt) remains byte-preserved, including
failed aggregate/native receipts and its publication status at delivery time.
It does not describe the later integrated main branch.

The two mailbox patches were applied independently to a temporary Git index at
their declared base. Both logical trees match their declared identities,
including final tree `056ff282eefdae6b5a3ef786805484c1ca44fa45`.
All 242 paths (including five deletions) then matched the supplied path manifest
in integration commit `b5f9de95929e83b305b5161881b58c4e2124c907`.
Newer benchmark and browser changes were disjoint and preserved. The
[integration receipt](integration.json) records the exact source/delivery hashes.

Independent Linux validation of the exact delivered paths passed the GCC
all-owner build, tools/platform/global checks, all five software-Vulkan lifetime
clients, compiler-host/alloc/install/DXIL, TSIR/bootstrap/header dependencies and
the changed-boundary sanitizer group. Actual Mac compiler-host/DXIL/TSIR checks
passed; retained Zig WGSL tests passed 1,077 with 22 skips. Windows strict SDK
syntax review passed 171 source units. These are their named developer scopes,
not exhaustive device or release qualification.

The first new Linux bootstrap unittest run executed one case and skipped seven
because its retained Zig oracle binary was unavailable in the isolated clone.
Those first skips do not establish differential parity. A fresh retained-Zig
oracle build and [differential rerun](new-linux-differential/summary.json) then
passed all eight manifest tests with zero skips, 67 bootstrap oracle checks,
seven target identities, 11 source-error checks, 30 semantic cases and 4,152
exact f64 decimal cases. The numeric probe initially failed to compile because
six calls did not propagate the retained Zig helper's error union. The test
driver repair preserves its inputs and expected outputs; the rerun passed
3,395 comparisons over 471 runtime inputs and 49 operand pairs. Both the first
failure and successful rerun are retained with source bindings. These are host
compiler differential checks, not physical GPU, browser or performance proof.
The original bundle's differential receipts retain their original identity.

The required C aggregate still rejects three pre-existing quarantined CSL test
registrations. The original bundle additionally retains its software-Vulkan MSAA
failure; it was not waived. The generic all-owner target on Mac stops at the
existing labels owner's GCC/Linux-only archive profile, so its Linux result does
not become a passing Mac aggregate.

Post-delivery platform review found and repaired Darwin feature declarations,
ASLR-sensitive Mach-O loaded-image hashing, explicit Windows C++ x64 targeting,
and CRLF conversion of managed Chromium patches. Focused tests exercise real
Mac cache reload/change/missing-identity behavior and actual Windows compiler
architecture. The portability changes are separate from the byte-exact imported
delivery. Full native rebuilds, matched performance and browser builds retain
their own source and artifact receipts; earlier platform results do not qualify
the changed bytes.
