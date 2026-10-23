# C label-owner style

Follow [common C style](../STYLE.md), the [frozen contract](CONTRACT.md) and
[native adapter subset](../../zig/STYLE.md#native-adapter-subset).

- Keep one typed entry/map owner; share native/static and standalone storage
  mechanics without a registry, macro framework, second map or object refcount.
- Copy arbitrary bytes exactly. Make key/length/allocation ownership explicit;
  preserve old bytes and pointer until a successful replacement can publish.
- Check ordinary arguments before closing-state rejection; reject closing-state
  no-ops without locks/hooks. Only exclusive destroy retry recovers cleanup state.
- Label allocator hooks under the mutation lock are an explicit source-bound
  exception. No callback reentry, loader work or new foreign callbacks under it.
- A returned view borrows after unlocking. Do not imply mutex protection of later
  dereference or conflate object lifetime with exclusion of label replacement.
- Keep private singleton bindings hidden and artifact-local; expose no test
  mutation API. Default builds must not import/link the selected C label owner.
- Preserve status handling, diagnostics and the original lifecycle order. Record
  inherited defects separately and keep independently sealed wrong-but-typed,
  allocation, cleanup, concurrency and actual native consumer evidence.
- Charge headers, build verification and transitional adapters; readable ownership
  and complete failure behavior take precedence over the size target.
