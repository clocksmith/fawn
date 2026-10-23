# Build lifecycle observation supplement

The original FCI canonical run reached its 900-second subprocess bound. The
outer exec returned 1 with TimeoutExpired; native driver/child exit is unknown.
A later tool poll reports an unknown completed session, but later exec process
namespaces cannot observe the original descendants. Closed-session lifecycle
supports an inference, not a receipt of descendant termination. No proven
uncontended claim is made for the retry.

The single permitted retry uses a wholly distinct source copy, cache, global
cache and install directory, with all 1,719 input bytes independently reverified.
Toolchain, archive inputs, flags, target and explicit environment are unchanged.
Its wrapper uses a new process group, samples that group within its namespace,
and records termination/reap and surviving-group state before any next build.
All original outputs/caches remain intact. Other pending runners gain the same
supervision without changing test inputs/assertions; FCI cache references move
to the isolated retry. Their original scripts and seals remain intact.
