# Evidence boundary

MIT-licensed independent tests authored against integration contract17a1fc3 before
candidate inspection, frozen in FROZEN_CASES.md. Original C owner b9b07df and Zig
future owner remain unchanged. The independent native_cases preinspection hash was
10299ca27a85dbc71271b42228f81549e78cd3d4c14d158a3606760c2c98d479.
Observer/failure/layout/concurrent-wait tests were added after implementation
inspection to execute already-frozen requirements. No developer test source was
used as the oracle. Tests call actual callback_delivery and native instance APIs.

Independent evidence lives outside the source tree at the campaign's evaluator
REPORT.md and JSON receipts. Fresh-source normal, C-ASan/UBSan and C-TSan retained
consumer variants each pass 12 active cases/1 guarded skip. The guard separately
aborts status8. Armed native pthread injection gives four NULL-return rollback
cases and four exceptional status13 aborts. Strict C/C++ native consumption and
private C++ layout consumption pass. Six unchanged default tests run separately.
An independent fresh selected headless shared-library source build additionally
passed, followed by C/C++ exported consumers and all eight armed native failure
cases. Its SHA256 is 008de1a7e4c71cc45d67e5c31de9514e51ad8d15da8709a982c098f5bcada2a7.
Earlier author-built selected/default consumption is retained separately; default
source-build and negative-selection receipts are reviewed author evidence.

C instrumentation excludes Zig code/allocators/refcounts; ASan leak detection is
disabled. Queue deinit is directly observed. Native instance heap free is tied to
actual unchanged allocator-destroy source, not allocator-interposed leak proof.
No GPU/device/backend execution, speedup, default promotion or whole-runtime
race/leak guarantee follows.

The 39,830-byte options fixture is exactly the generated selected headless build
input used in independent compilation, hash recorded in BUILD_OPTIONS.sha256.
It contains frozen source-derived policy declarations needed to compile actual
retained modules. It is not regenerated from private notes or an archived cache
at reproduction time. Source files are supplied from an explicit hash-matching
checkout and copied into output solely to honor Zig module-root restrictions.
Final compact harness verification uses input-hash/import/plan checks and a small
native prebuilt consumer; the full test compiles were already performed against
the equivalent preserved source snapshot and options, not repeated for packaging.
