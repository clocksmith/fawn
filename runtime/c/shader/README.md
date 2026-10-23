# Owned C shader results

Authored and focused-host-checked, not integrated or independently qualified.
This is the persistent ownership half of the standalone C compiler boundary:
one compilation pipeline, copied source/compiler identity, exact immutable target,
types, resource layouts and separate entry closures. It follows the allocation,
clone/deinit and entry-binding responsibility of the retained
[TranslationInfo owner](../../zig/src/compiler/wgsl/runtime/runtime_translation_info.zig),
without pretending that standalone reflection supplies its dispatch/texture
preconditions, Metal size-buffer policy or complete production cache semantics.

The caller supplies explicit compiler capacities and allocator hooks. Two compiler
calls measure then publish; no source re-parser or alternate semantic engine.
The temporary workspace is released before publication. One owned allocation
contains the retained result. Identity matching compares exact source, explicit
compiler identity and entry selection, not hashes alone. It is not a cache.
Inputs/output/error spans must be valid, disjoint and stable through the call;
allocator hooks return fresh suitably aligned storage and may not mutate inputs.
Retained views require a live reference. No GPU or device lifetime is acquired.

Build the C compiler first, then run (paths may be absolute):

```sh
make -C runtime/c/shader test BUILD=/tmp/doe-shader \
  COMPILER_LIBRARY=/path/to/libdoe_compiler.so
make -C runtime/c/shader sanitize BUILD=/tmp/doe-shader \
  COMPILER_LIBRARY=/path/to/libdoe_compiler.so
```

The focused test covers two entry closures, exact identity, copied source,
allocation failure at both acquisition points, error cleanup and retained views.
Sanitizers instrument this owner; the selected compiler library has its own
instrumentation status. LeakSanitizer is disabled in the ptrace cloud environment;
allocation counters check this owner's cleanup, not universal leak freedom.
The first local test exposed an adapter mistake: ABI5 measurement requires a NULL
output descriptor. That was corrected without changing compiler semantics.
