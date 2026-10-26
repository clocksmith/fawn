# Windows DXC runner: verification note

Status: implemented in code, Windows execution unverified.

Commit verified: 31d954557cd1eb990c79566606ce0ea6bc9bdf27, "Implement Windows DXC process execution with bounded diagnostics, explicit handle inheritance, and real shader compilation controls", on clocksmith/fawn main, parent of the CSL push 5678df1ef9f12faa8c7895201f8718500f6f8122.

What changed in that commit, from its diff:
- runtime/c/compiler/src/dxil/dxil_process.h (new, 12 lines): shared discovery diagnostic, dxil_dxc_missing_message, no process state retained.
- runtime/c/compiler/src/dxil/dxil_process_windows.c (new, 277 lines): dxil_dxc_run on Windows using CreateProcessW, no shell, UTF-8 argument conversion with CRT quoting, 32767 UTF-16 unit command cap, explicit inherited-handle list (input, stdout, stderr), concurrent bounded stdout/stderr draining, complete process cleanup, bounded diagnostics, full unsigned exit code reporting.
- runtime/c/compiler/src/dxil/dxil_toolchain.c and dxil_toolchain.h: removed the Windows stub that returned DXIL_SHADER_TOOLCHAIN_UNAVAILABLE with "DXC invocation is not implemented for Windows in this port", and shared dxil_dxc_missing_message between POSIX and Windows paths. POSIX fork/exec runner unchanged.
- runtime/c/compiler/src/dxil/gaps.md and parity-test.md: updated to state the Windows implementation exists, with Windows execution still unverified.
- runtime/c/compiler/src/dxil/tests/dxc_process_probe.c and runtime/c/compiler/tools/test_dxil_windows.py: process boundary and real child probe, requiring actual DXC compilation and container validation.

What this closes, and what it does not:
- Closes: "Windows DXC runner is explicitly unimplemented" as a code gap. The stub is gone.
- Does NOT close: Windows execution. No Windows run is claimed from this Linux VM, and no Windows test result is recorded in this note. The commit's own gaps text says Windows execution remains unverified until that control runs on Windows, and this process implementation alone does not close adapter, D3D12, or release gates.
- Does NOT change: CSL status. CSL remains untested, not wired into sources.txt or libdoe_compiler.so, and parity not verified. DXIL bounded frontend/HLSL and documented semantic differences remain separate gaps.

Next step to fully close the DXC item: run tools/test_dxil_windows.py on Windows with a real DXC, and record its result here.
