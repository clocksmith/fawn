# Futures C implementation style

Follow the repository native adapter subset and the frozen ABI/lifetime contract.

- Use C11 typed queue/completion state and explicit status values. The only erased
  domain pointer is callback userdata; storage pointers exist solely at init.
- Keep caller allocation separate from synchronization ownership. Track each
  initialized pthread member until its destroy succeeds. Recovery admits only
  exclusive deinit and never discards a still-owned member.
- Hold the queue mutex for list/identity/delivery state, never foreign callbacks.
  Keep the active operation counted across each unlocked callback or wait. Save
  callback inputs before dispatch and never touch its potentially freed node again.
- Treat the caller lifetime/external-exclusion requirements as real preconditions;
  mutexes cannot protect freed nodes or queues. Do not imply cancellation support.
- Check namespace and timespec arithmetic before mutation or narrowing. Preserve
  the source's per-selection ordering, issuance boundary and timeout race result.
- Keep production limits exact. Exhaustion testing may change only private build
  constants behind DOE_FUTURES_TESTING, never add a public mutation API.
- Retain first failures separately from repaired results. Native API tests use
  caller storage and the built static library; instrumentation wraps platform
  boundaries without implementing a second completion queue.
