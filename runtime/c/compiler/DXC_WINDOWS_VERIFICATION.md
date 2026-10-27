# Windows DXC runner: verification note

Status: the imported Windows process runner was exercised on Miles with real
DXC. The allocating host and native-16 option also passed their named controls.
Full compiler, D3D12, browser and release qualification remain separate.

The [current execution record](../../../reports/maintenance/20261026-noncsl-c-parity-delivery/new-windows-execution/summary.json)
binds source `e5d8ffc6aed94a2aaa7fd59ab92edaec369b08a7`: 40 process controls,
real DXC compilation and container validation/dumpbin, invalid-HLSL rejection,
and the handle-leak control passed. The runner SHA-256 is
`16dd9637e199ec9e5ba90fc30d38f5a34bb825c31fc64a16ca290a99024b6348`.
The original raw summary reports Miles's enclosing active checkout. Its separate
source binding verifies all 35 tested input files against the immutable archive;
the raw metadata has not been rewritten.

The allocating host DLL, ordinary C/C++ consumers, bootstrap/oracle controls and
real source-to-DXIL ABI passed after a build-driver-only repair separating compile
and link commands. The retained overlay records its exact bytes. The controlled
I/O test passed 103 checks, including native-16 option propagation. This does not
establish physical half-precision GPU execution. The first build failure and
successful controls retain their own evidence.

The original inspection below was performed from a Linux VM. Later fleet
[Windows results](../../../reports/fawn-workspace/20261026-platforms/windows-dxc/summary.json)
record 40 process controls, real DXC compilation/dumpbin, invalid-HLSL rejection
and stable handle count. The [inputs](../../../reports/fawn-workspace/20261026-platforms/windows-dxc/inputs.json)
bind exact tested bytes and tool hashes. They record commit `5678df1e` plus
explicit validator/test-driver changes; they are not a clean-current-tree claim.
The imported parity delivery subsequently added `dxil_dxc_run_options` and its
native-16 argument; the fresh record above exercises those changed bytes.

Commit verified: 31d954557cd1eb990c79566606ce0ea6bc9bdf27, "Implement Windows DXC process execution with bounded diagnostics, explicit handle inheritance, and real shader compilation controls", on clocksmith/fawn main, parent of the CSL push 5678df1ef9f12faa8c7895201f8718500f6f8122.

What changed in that commit, from its diff:
- runtime/c/compiler/src/dxil/dxil_process.h (new, 12 lines): shared discovery diagnostic, dxil_dxc_missing_message, no process state retained.
- runtime/c/compiler/src/dxil/dxil_process_windows.c (new, 277 lines): dxil_dxc_run on Windows using CreateProcessW, no shell, UTF-8 argument conversion with CRT quoting, 32767 UTF-16 unit command cap, explicit inherited-handle list (input, stdout, stderr), concurrent bounded stdout/stderr draining, complete process cleanup, bounded diagnostics, full unsigned exit code reporting.
- runtime/c/compiler/src/dxil/dxil_toolchain.c and dxil_toolchain.h: removed the Windows stub that returned DXIL_SHADER_TOOLCHAIN_UNAVAILABLE with "DXC invocation is not implemented for Windows in this port", and shared dxil_dxc_missing_message between POSIX and Windows paths. POSIX fork/exec runner unchanged.
- runtime/c/compiler/src/dxil/gaps.md and parity-test.md: updated to state the Windows implementation exists, with Windows execution still unverified.
- runtime/c/compiler/src/dxil/tests/dxc_process_probe.c and runtime/c/compiler/tools/test_dxil_windows.py: process boundary and real child probe, requiring actual DXC compilation and container validation.

What this closes, and what it does not:
- Closes: "Windows DXC runner is explicitly unimplemented" as a code gap. The stub is gone.
- The Linux VM inspection alone did not close Windows execution. The separately
  retained Miles run establishes its recorded process boundary; neither that
  result nor source inspection closes adapter, D3D12, browser or release gates.
- Does NOT change: CSL status. CSL remains untested, not wired into sources.txt or libdoe_compiler.so, and parity not verified. DXIL bounded frontend/HLSL and documented semantic differences remain separate gaps.

Further runner changes require `tools/test_dxil_windows.py` on Windows with real
DXC and fresh source bindings. Allocating host changes also require its ordinary
ABI and controlled I/O checks; neither test group grants GPU or release admission.
