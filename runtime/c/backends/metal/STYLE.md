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
  one bit per buffer, or one per texture mip/layer (one full volume for 3D).
  Only successful native completion may publish it. Error/unknown must not become
  initialized or readable output. Render STORE preserves only loaded or cleared
  subresources; DISCARD never proves complete shader coverage. Reject combined
  depth/stencil aliases while initialization is not aspect-qualified. Discard
  native recordings before their leases.
- Validate a complete render pass before native mutation; bind explicit per-stage
  snapshots and clear absent native slots between draws. No open encoder escapes
  the call. Reuse the compute buffer/image checks and shared MSL function builder
  where semantics agree rather than creating another binding or source policy.
- Keep device/public validation separate. This API accepts already validated MSL
  and explicit compiler slot/workgroup facts, not source-derived heuristics.
- Tests may replace the private native call table or exact typed message calls;
  no production constructor accepts a simulator. Assert identities and call
  ordering, not invented GPU results. Document SDK and physical gaps separately.
