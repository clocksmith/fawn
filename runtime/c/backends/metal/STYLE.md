# Metal native C style

The [common guide](../../STYLE.md) remains authoritative. This owner adds only
Metal-specific boundary rules:

- C Objective-C calls use the exact typed selector signature. A function pointer
  conversion belongs only in the loader; method callers use named typed fields.
  MTLSize/NSRange are explicit NSUInteger records, never byte-sized placeholders.
- Create/new objects return one native reference; autoreleased command buffers
  are retained before their pool drains. Encoders are borrowed only within the
  encoding call and must end before the pool drains. Temporary library/function
  and NSString ownership stays inside the pipeline constructor.
- A speculative initialization bit belongs to the recording's buffer-use entry.
  Only successful native completion may publish it. Error/unknown must not become
  initialized or readable output. Discard native recordings before their leases.
- Keep device/public validation separate. This API accepts already validated MSL
  and explicit compiler slot/workgroup facts, not source-derived heuristics.
- Tests may replace the private native call table or exact typed message calls;
  no production constructor accepts a simulator. Assert identities and call
  ordering, not invented GPU results. Document SDK and physical gaps separately.
