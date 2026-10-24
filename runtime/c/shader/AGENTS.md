# Owned shader boundary

Read the parent C guide and this component's [charter](CATSCAN.md). Compiler
ABI13 validates the whole symbolic module; specialize the same saved source and
policy through its typed API. Never substitute source, reinterpret stage layouts,
or infer executable support from module validation.

Keep module-only and executable borrows distinct, with untouched outputs on
rejection. A specialized artifact has one compiler-selected entry at index0 and
its original declared name/stage. It owns its code and exact key/value snapshot,
and retains the immutable module through the existing tagged reference owner.
Compare double representations as bytes, preserving signed zero; conservative
ordered identity is allowed, inferred equivalence is not. Retain legacy creation.

Use the same target/semantic flags and stable input snapshots in measure and fill.
Revalidate post-allocator row lengths before copying into an admitted extent; never
reread a mutable caller extent to size or advance an owned buffer. Source, identity
and selection bytes must be saved before the first compiler pass.
Acquire privately and publish only after temporary input/workspace cleanup. Every
acquisition failure preserves caller outputs and other live artifacts. Allocator
hooks/userdata outlive every handle; retained views require a live reference.

The Makefile owns the complete shader source inventory. C/C++ and sanitizer
controls are host evidence; target SDK, native consumers and physical execution
keep their own scope. No owner check starts GPU work implicitly.
