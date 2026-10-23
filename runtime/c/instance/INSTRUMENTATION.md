# Instance acceptance instrumentation clarification

This is an additive clarification of the already frozen requirement to inject
failure at the actual retained DoeSurface allocation, not a new production
allocator or lifetime policy. The original contract and seal remain unchanged.

The existing surface owner has no injectable allocation binding. A test-only
`test_surface_allocator` binding in `doe_surface_native.zig` supplies the allocator
only when `builtin.is_test`. It feeds the actual `native_helpers.create(DoeSurface,
allocator)` call, preserving the existing creation observation, and both actual
surface destruction paths. Production still uses `native_helpers.alloc` exactly
as the old `make(DoeSurface)` route did. No simulated surface/device is substituted.

A test must keep this allocator binding and its state valid and unchanged from
allocation through destruction, and externally serialize changes to the binding.
It must not change the binding while another test thread can create or destroy a
surface. Allocation failure happens before instance retain and is observed through
the actual native consumer; successful test allocations use matched destruction.
This does not qualify retained Zig allocation under C-only sanitizer instrumentation.

For invalid-device rejection fixtures, readable storage must cover every field
accessed by the retained non-extern DoeDevice recognizer and meet its alignment.
A four-byte marker alone does not establish that precondition. Initialized,
properly sized/aligned nonmatching storage may test rejection; it must never be
used as a fabricated valid device. Preserve the first invalid fixture failure
and disclose its correction separately from candidate implementation fixes.
