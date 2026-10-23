# Accepted default-off native C futures integration

The [frozen contract](CONTRACT.md) at `17a1fc3` passes independent CPU integration
acceptance. The C queue now has real retained runtime consumers on the isolated
experimental branch, not just standalone fixtures. Production default remains
the original Zig owner. This is a mixed-language runtime, not a Zig-free product.

## Actual ownership and build path

Five producer/type imports select one identity domain. The real instance creation
and final-release paths explicitly initialize/deinitialize the selected queue.
The private bridge uses actual C types with checked C/Zig sizes, alignment and
every field offset; there is no guessed byte storage or mirror pending list.
Callbacks may retire and free their payload before returning. Saved full-call
instance leases protect the queue through the C return, including worker
publication. The trampoline preserves dispatch assignment after registration.
WaitAny owns a checked typed copy rather than aliasing unlike structs.

GCC alone builds the C owner/bridge archive. Approved Zig 0.15.2 compiles the
retained consumers and existing backend stubs, not the C owner. Omitting the
archive flag removes the C owner/header/archive dependency. Unsupported selected
build combinations reject explicitly; they do not mix identity providers.
Per-API payload policy, device/backend objects and physical completion remain
with their retained owners.

## Independent evidence

The evaluator froze E01–E16 before candidate inspection and independently authored
actual native/callback tests. No candidate implementation defect was found.

- Fresh-source retained-consumer builds pass 12 active tests in normal, C-ASan/
  UBSan and C-TSan variants. Each has one explicitly skipped fatal guard; a
  separate subprocess exercises that guard and aborts with queue-deinit status 8.
- Tests execute actual heap instances, callback_delivery payloads, last-caller
  release without observer leases, worker publication, reentry, simultaneous
  waiter/producer, self versus other-thread waits, issuance-boundary ordering,
  duplicate/foreign IDs, OOM, overflow and late dispatch/free.
- An independently source-built selected headless shared library passes separately
  GCC/G++-built native consumers. Four setup failures return NULL with observed
  rollback; four exceptional cleanup failures abort with status 13, including
  failed acquisition followed by failed cleanup. No reserved storage is
  misreported as successfully cleaned.
- Strict independent C++ private-layout/cleanup consumption passes. Six unchanged
  original-owner tests pass by independent execution of the author-built default
  test binary. The default DSO has no C owner/bridge symbols. Default build and
  negative-selection source receipts are reviewed developer evidence, not
  relabelled independent rebuilds.

The first shared-library checks consumed the author's artifact. Final review
caught that the evaluator's own E02 wording required an independent shared source
build; it then performed that build and repeated native/fault checks against the
fresh artifact before acceptance. Earlier prebuilt results remain separately
labelled. Initial injector attempts also affected loader mutexes; those receipts
remain, followed by correctly armed native-call-only injection. Neither event
was a candidate defect.

Sanitizers instrument the C queue/private bridge, not retained Zig code,
allocators or reference counts. LeakSanitizer is unavailable. Queue deinit is
directly observed; native instance heap free is source-bound to the actual
allocator-destroy path, not allocator-interposed leak proof. These limits forbid
whole-runtime race/leak or physical GPU claims.

## Reproduction, identity and measured cost

The [compact independent suite](independent_tests/README.md) contains no copied
production implementation or binaries. It takes an explicit hash-matching
checkout, verifies the actual import closure, and copies sources only into fresh
output to satisfy Zig module-root rules. Its frozen generated-options fixture is
source-derived test input, not a second production policy owner. The optional
`--build-shared` route rebuilds the actual headless artifact. All tools are caller
supplied; nothing installs or downloads.

Implementation manifest SHA256:
`44dd226c747587a8945463c25c0ee00884d113018563a9fede3bffe489d203d0`.
Independent shared artifact SHA256:
`008de1a7e4c71cc45d67e5c31de9514e51ad8d15da8709a982c098f5bcada2a7`
(12,912,952 bytes). Its source build took 60.889 seconds with peak child RSS
817,556 KiB. Focused independent native build/run took 17.355 seconds and
324,576 KiB. These are observations, not matched speed comparisons.

The independent report and exact commands/receipts are retained in isolated
`futures2/evaluator`; [developer provenance](PROVENANCE.md) separately records
original failures, build costs and unchanged-owner evidence. Final-location
package/source checks, prepare-only command generation and a native prebuilt smoke
pass without repeating the campaign. One initial lead smoke used an incorrect
artifact filename and failed before loading any library; the failure and corrected
exact-hash invocation are preserved. No implementation changed for packaging.

## Remaining migration boundary

One CPU queue owner is experimentally integrated, but zero production-default
owners have switched. The adapter/facade add transitional Zig source, not a line-
count reduction. Payload ownership migration, physical producer/device/backend
execution, wider native APIs and consumers, performance and release gates remain
open. The original Zig queue and accepted standalone C queue are byte-identical.
