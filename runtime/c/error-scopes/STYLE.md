# Error-scope C style

Follow [common C style](../STYLE.md) and the [exact contract](CONTRACT.md).

- Plain C11 typed state, seven small public functions, explicit status and original
  pthread error. No heap, global registry, generic framework or second classifier.
- Use real caller-allocated or imported typed storage, never byte-array casts.
  Initialize at the final address; never copy/move an initialized mutex.
- Snapshot result/handler/userdata under lock. Call foreign code only after unlock
  and never access the stack after dispatch, including cleanup or counters.
- Keep first-error, filter, 64-depth and 512-byte obligations exact. Unknown numeric
  types and embedded NUL bytes are data, not new policy or string terminators.
- Check argument/state precedence before mutex access. Preserve errno on every
  callback-free nonterminal return and leave callback-induced errno visible.
- Deinit failure retains storage/ownership for exclusive deinit retry only.
  Unexpected native operational statuses terminate with original diagnostics.
- Keep baseline classification and standard future/payload/device owners separate;
  every transitional adapter and build input remains charged migration work.
