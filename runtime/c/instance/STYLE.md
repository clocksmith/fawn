# Native instance C implementation style

Follow the original contract and [capability amendment](CAPABILITIES.md), repository native adapter subset and explicit
[future-owner style](../futures/STYLE.md).

- Keep allocation/reference ownership in typed C state. The public instance is
  opaque; no layout cast from a retained Zig instance or mirrored atomic count.
- Snapshot allocator, hook and lease values before a callback/free can invalidate
  their former container. Borrowed queue views never acquire an implicit lease.
- Check alignment, allocation and wait-copy arithmetic before mutation/narrowing.
  Use distinct typed WGPU/future records, never layout-only aliasing.
- Keep functions small and domain-specific, with descriptive names, ordinary
  indentation and one nontrivial state change per line. Avoid code golf, generic
  macro frameworks, hidden globals and mechanical line-for-line translation.
- Preserve the actual inherited atomic orders and external-texture guard. C alone
  decides finalization. No hook may resurrect unpublished or dying state.
- Treat terminal uncertain cleanup explicitly; never relabel it rollback or free
  storage containing still-owned synchronization.
- Inject failures at real allocation/platform boundaries; do not replace accepted
  queue policy with a test model or add public state mutation APIs.
- Keep default/selected source identity, first failures and instrumentation limits
  explicit. A smaller line count is not maintainability or performance evidence.
