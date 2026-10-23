# Private count-boundary binding supplement

Expectations C08 were sealed before implementation. The exact GCC-only source
handoff disclosed the private references field and test compilation gates
DOE_INSTANCE_TESTING/DOE_INSTANCE_TEST_REFERENCE_LIMIT. This fixture binds those
already frozen max/zero/undo cases after inspecting only the sealed C core. It is
not original blind fixture code. No candidate source changes, public mutation API,
stale handle or private C layout overlay on a native Zig object is used.

Private zero/max fixtures retain allocated readable storage solely to drive
isolated diagnostic termination; these are not examples of legal public use.
Reduced limit3 is the sealed C08 input. A separate normal-limit build checks
UINT32_MAX. A successful bounded control checks balanced retain/release/undo.
Every fatal must abort with count diagnostics and without label removal or free.
Native candidate source remains uninspected. A prior attempt to create these
files was not executed due to a transient exec-server disconnect; absence was
verified before the identical setup was retried successfully.
