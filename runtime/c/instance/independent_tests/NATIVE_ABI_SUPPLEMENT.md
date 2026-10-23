# Retained ABI supplement

Post-handoff realization of sealed P06/B01: compare all fields, scalar widths,
alignments, offsets, record sizes of the actual retained base/callback/upstream
Future and WaitInfo types, capability feature/limit records and scalar aliases
against the pinned C header. Invoke actual C capability by-value returns through
the retained adapter, and exact GCC future completion/queue layout probe.
The original complete frozen-header C/C++ and Zig ABI probes remain unchanged.
`instance_extent.c` additionally observes actual normal C allocation extent
through its public allocation hook; no private C object overlay or hard-coded
allocation size is used to create an instance. Its observed extent is264bytes.
