# D3D12 C owner

- Read the common C guide and this directory's charter. Native WebGPU object
  validation and shader meaning remain outside this backend-private ABI.
- Use Windows SDK C declarations in `windows_*.c`; do not construct replacement
  COM vtables, spoof `_WIN32`, or call host controls an SDK/driver check.
- The serialized context is the sole submitted-command retirement owner.
  Execute first publishes that ownership; failed signal/wait leaves every
  obligation intact. Only observed completion or terminal device loss settles it.
- A buffer or texture is exclusive to one recorded or unresolved command. This deliberately
  rejects cross-command resource reuse until completion, rather than pretending
  to support arbitrary overlapping command lists or WebGPU usage scopes.
- Every default-buffer/texture operation begins/ends at COPY_DEST; upload/readback heap
  states stay fixed. Descriptor heaps and allocators survive their whole submission.
- Test host ownership at the private typed native-call link seam, not a simulated
  driver or shader interpreter. Keep actual Windows readback as a separate fixture.
- Texture formats, whole-subresource copies and immutable views remain an explicit
  subset. Share checked footprint arithmetic with the common leaf, but keep D3D12
  alignment/format admission here. Never publish readback padding as initialized
  by a texture copy; it must already be initialized or absent.
- Sampled/storage descriptors and static samplers are pipeline/command-owned, not
  a second resource registry. Dynamic views/samplers need a real consumer before
  broadening the boundary. Preserve historical source seals when extending it.
