# Host primitive implementation

Read the parent C guide and this owner's [contract](README.md).

- Keep native handles and partial construction bits in these leaves. Callers own
  exclusion and policy; never add a queue, scheduler, allocator or refcount here.
- Preserve real POSIX symbol calls so existing GNU linker fault controls continue
  to intercept the actual boundary. Never compile a synthetic Windows branch on
  POSIX as SDK evidence.
- A failed constructor can still own attributes. Always destroy partial state,
  including after SYSTEM_ERROR; retain storage on failed cleanup.
- Thread startup borrows its record through entry acknowledgement or successful
  join. Failed join/detach preserves the handle. Successful detach closes the
  handle, not the worker's lifetime.
- Check `make check`; on GNU/Linux also `make check-faults` and the changed
  consumers' existing owner tests. Windows uses the same shared CPU source and
  actual SDK build route owned by the parent integration workflow.
