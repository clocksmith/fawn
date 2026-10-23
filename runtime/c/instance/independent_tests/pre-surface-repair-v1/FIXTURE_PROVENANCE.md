# Fixture seal 1 provenance

All files named by FIXTURES_SEAL.sha256 were written before any Instance 1
candidate implementation source was inspected or executed. Only the immutable
frozen contract/header and accepted predecessor source/fixtures were read.
Cases inventory was sealed separately before implementation permission.

Public C/C++ source syntax was checked using GCC/G++ and the frozen header.
Native Zig source was formatted, not compiled against a candidate before seal.
No behavioral result is claimed merely from this fixture seal or syntax pass.

build_options.fixture.zig is the exact accepted callback fixture from
3582e86472db945cc441eedb3796ec7f1b13d179 plus the one explicit
experimental_c_instance=true declaration. It is a private focused-test build
input, not a substitute for canonical generated options in four source-built DSOs.
The C and Zig ABI probes emit the full public crossing layouts independently;
comparison sorts lines because their emitted field/type grouping differs.

The scope remains the 32 groups in CASES.md with pre-candidate baseline precision
clarifications in BASELINE_CLARIFICATIONS.md. Test implementation supplements,
compile repairs, failures, final candidate source identities and exact run coverage
will be retained separately. No post-hoc fixture change may silently replace this
version. Test-source backup must precede repairs after candidate handoff.
