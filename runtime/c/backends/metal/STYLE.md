# Metal native C style

The [common guide](../../STYLE.md) remains authoritative. This owner adds only
Metal-specific boundary rules:

- C Objective-C calls use the exact typed selector signature. A function pointer
  conversion belongs only in the loader; method callers use named typed fields.
  MTLSize/MTLOrigin/NSRange are explicit NSUInteger records. Swizzles are four
  uint8 channels, not four NSUInteger fields or guessed opaque storage.
  Render clear colors use four doubles, viewport six doubles, and native scissor
  four NSUInteger fields. Stencil masks/clear/reference use uint32; indexed base
  vertex uses signed NSInteger. Do not route these through a generic size setter.
- Create/new objects return one native reference; autoreleased command buffers
  are retained before their pool drains. Encoders are borrowed only within the
  encoding call and must end before the pool drains. Temporary library/function,
  NSString, texture-descriptor and sampler-descriptor references stay local.
- Speculative initialization belongs to the recording's resource-use entry:
  one bit per buffer, or color/depth/stencil bits per texture mip/layer (one full
  volume for 3D).
  Only successful native completion may publish it. Error/unknown must not become
  initialized or readable output. Render STORE preserves only loaded or cleared
  aspects; DISCARD never proves complete shader coverage. Resolve publication
  changes the target aspect independently of source preservation. Combined
  attachments require both aspects explicitly represented in the pass. Discard
  native recordings before their leases.
- Attachment initialization reads those same speculative bits, preflights all
  selected subresources, then records real zero clears for missing aspects and
  LOAD/STORE for initialized companions. It never adds a frontend availability
  flag or publishes state before terminal success.
- Empty logical viewport/scissor lowers to safe native raster state with an empty
  scissor while the original nonempty draw still executes. Alpha-to-coverage uses
  the typed BOOL pipeline selector. This owner has no fixed-function partial
  sample-mask operation; such masks reject before native mutation.
- Validate a complete render pass before native mutation; bind explicit per-stage
  snapshots and clear absent native slots between draws. No open encoder escapes
  the call. Reuse the compute buffer/image checks and shared MSL function builder
  where semantics agree rather than creating another binding or source policy.
- Query visibility modes/offsets must form non-reentrant contiguous runs. Do not
  repeatedly reset a native offset between draws in the same run. Require
  initialized result storage, retain it normally, and never synthesize a count.
- Timestamp availability is a speculative per-sample command snapshot. An index
  is written at most once by strict primitives per recording. Explicit ordered
  boundaries may rewrite after a native event orders preceding work and resolves;
  other reuse waits until its prior owner settles. A native error poisons the sample buffer; no second completion
  path publishes it. Resolving records does not certify non-error counter values.
- Keep device/public validation separate. This API accepts already validated MSL
  and explicit compiler slot/workgroup facts, not source-derived heuristics.
- Terminal error operation, native code and bounded copied message belong to the
  existing command failure snapshot. Capture the NSError while its pool is live,
  before retiring native/resource obligations. A later poll or caller error output
  must not replace the first snapshot or retain an Objective-C string pointer.
- Tests may replace the private native call table or exact typed message calls;
  no production constructor accepts a simulator. Assert identities and call
  ordering, not invented GPU results. Document SDK and physical gaps separately.
