# Experimental C drop-in policy owner

Status: contract frozen; implementation and independent acceptance pending.
Default production routing is unchanged. This is not a Debug crash fix or GPU qualification.

- [Contract](CONTRACT.md): process-once routing, separate lazy states, descriptor lifetime and ABI.
- [Scope](SCOPE.md): actual consumer and three explicit semantic/failure-site differences.
- [Header](include/doe_dropin_policy.h): independently usable C boundary.
- [File map](FILE_MAP.md): selected native adapter and build dependency closure.
- [Acceptance obligations](ACCEPTANCE.md): required controls before acceptance.
- [Source inputs](SOURCE_INPUTS.json) and [installation](INSTALLATION.json): exact retained identities.

Verify the installed freeze from this directory with `sha256sum -c CONTRACT.sha256`.
Independent cases must be sealed before implementation. The planned GCC-only archive
will be selected by an explicit default-off native build option. Exact runnable
build/test commands belong here when implemented; none are claimed yet.
