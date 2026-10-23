# Metal native C style

The [common guide](../../STYLE.md) remains authoritative. This owner adds only
Metal-specific boundary rules:

- C Objective-C calls use the exact typed selector signature. A function pointer
  conversion belongs only in the loader; method callers use named typed fields.
  MTLSize/MTLOrigin/NSRange are explicit NSUInteger records. Swizzles are four
  uint8 channels, not four NSUInteger fields or guessed opaque storage.
- Create/new objects return one native reference; autoreleased command buffers
  are retained before their pool drains. Encoders are borrowed only within the
  encoding call and must end before the pool drains. Temporary library/function,
  NSString, texture-descriptor and sampler-descriptor references stay local.
- Speculative initialization belongs to the recording's resource-use entry:
  one bit per buffer, or one per texture mip/layer (one full volume for 3D).
  Only successful native completion may publish it. Error/unknown must not become
  initialized or readable output. Discard native recordings before their leases.
- Keep device/public validation separate. This API accepts already validated MSL
  and explicit compiler slot/workgroup facts, not source-derived heuristics.
- Tests may replace the private native call table or exact typed message calls;
  no production constructor accepts a simulator. Assert identities and call
  ordering, not invented GPU results. Document SDK and physical gaps separately.
