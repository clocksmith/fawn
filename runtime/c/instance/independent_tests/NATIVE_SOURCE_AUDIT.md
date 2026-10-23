# Fixed native source audit, before execution

Source seal 51347ba22f88c12724af95a6d87d821615b86782855ca8b4fa1e892cbe584669,
1,693 verified dependencies copied to private /tmp. Review found no native
ownership defect in the changed source. This is source evidence, not execution
acceptance. The heavy Zig build hold is still active.

- Selected DoeInstance aliases the opaque imported C type. Native cast recognizes
  its C prefix, AddRef delegates to its count, Release delegates to its guarded
  finalizer. Attempting generic object_should_destroy on it is a compile error.
- Native create installs copied real object/wait allocators and observation/label/
  unchanged external-texture hooks. C queue layout verification runs before
  native publication. No Zig instance allocation, shell, count or pending queue
  is created on FCI. Old selections still use their original typed queue/count.
- Future Completion owns a copied optional QueueView. Registration stores the
  value before real C registration. QueueView only contains the actual queue
  pointer and optional lease. It owns neither queue nor reference.
- Completion.markReady snapshots/acquires its lease and never dereferences self
  after the real C readiness call. Trampoline loads late dispatch then does not
  access the payload afterward. Direct view pump/wait snapshot their lease;
  wait frees copied records before final lease release. C native wait/pump own
  their own full-call protection.
- C callback adapter obtains the actual selected queue/lease value and lets the
  accepted C payload owner copy it. It introduces no second payload or scheduler.
- Both actual compute/render requests issue/register through instance_pending,
  retain an independent request instance lease and install late dispatch at
  settlement. Settlement snapshots the next request before publication. This
  is host ownership review, not successful pipeline evidence.
- Actual device-loss Event changes only its queue-reference argument. Its
  instance retain, real pending registration, finish/deinit, event free and
  pending instance release retain their ordering; null-device fixtures test the
  actual owner without fabricating a device.
- Both external-texture allocation rollback sites invoke nonfinal undo_retain.
  Registry lookup, positive-count guard and successful registration/cleanup
  policy stay with the retained native owner. Real imported textures remain an
  unexecuted physical gate.
- Actual surface create still allocates the real host surface before retaining
  an instance. Bind failure releases the instance then destroys the surface.
  Final surface release drops its independent instance lease. The declared
  allocator override is consulted only under builtin.is_test and used for both
  cleanup sites. Production trace/allocation remains native_helpers.create.
- Instance trace uses the original logical DoeInstance spelling explicitly and
  the actual allocation extent for each owner. No object dereference occurs in
  the preinitialization trace hook. This was an author-disclosed repair.
- Future_ids and callback_delivery source bytes equal predecessor3582e864; exact
  hashes are recorded. Original eleven selected future and five selected callback
  tests remain. The new suite contains23 named tests: eleven adapted futures,
  five adapted callbacks and seven additional instance obligations.
- build.zig sets all three owner flags on main/compute/full options, configures
  each former futures root with the same explicit archive triple, rejects absent
  dependencies, nonabsolute/missing archives and nonnative target; browser wasm
  and incompatible generic/old focused suites fail explicitly. Default imports
  remain compile-time unselected. Execution and symbols must still prove this.

Structural checks already passed from the fixed source copy: source layout, core
import fence, generated test inventory and changed Zig formatting. CATSCAN/index
passed29 charters against an independently bound supplemental structural tool/
charter/index snapshot and repository link-existence checks. These are not GPU
or semantic qualification. Receipts are in native-structural/.
