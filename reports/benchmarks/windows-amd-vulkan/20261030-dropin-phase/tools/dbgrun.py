"""Run an executable under the Win32 debug API; on 0xC0000409 (or any second-chance
fault) record the fast-fail code, faulting RIP and a scan of return addresses on the
stack, each as module + RVA, then symbolize with llvm-symbolizer. Diagnostic only.
dbgrun.py <cwd> <exe> [args...]  (env from the caller)"""
import ctypes, ctypes.wintypes as wt, sys, struct, subprocess, os
from pathlib import Path
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenThread.restype = wt.HANDLE; k.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k.GetFinalPathNameByHandleW.argtypes = [wt.HANDLE, wt.LPWSTR, wt.DWORD, wt.DWORD]
k.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
k.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
cwd, exe, args = sys.argv[1], sys.argv[2], sys.argv[3:]
class SI(ctypes.Structure): _fields_ = [('cb', wt.DWORD), ('r', wt.LPWSTR), ('d', wt.LPWSTR), ('t', wt.LPWSTR)] + [(f'x{i}', wt.DWORD) for i in range(8)] + [('w', wt.WORD), ('c', wt.WORD), ('p', ctypes.c_void_p), ('i', wt.HANDLE), ('o', wt.HANDLE), ('e', wt.HANDLE)]
class PI(ctypes.Structure): _fields_ = [('hp', wt.HANDLE), ('ht', wt.HANDLE), ('pid', wt.DWORD), ('tid', wt.DWORD)]
si = SI(); si.cb = ctypes.sizeof(si); pi = PI()
cmd = ctypes.create_unicode_buffer(subprocess.list2cmdline([exe, *args]))
if not k.CreateProcessW(None, cmd, None, None, False, 0x2, None, cwd, ctypes.byref(si), ctypes.byref(pi)):  # DEBUG_ONLY_THIS_PROCESS
    sys.exit(f'CreateProcess {ctypes.get_last_error()}')
hproc = pi.hp; mods = {}
def read(addr, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    k.ReadProcessMemory(hproc, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)); return buf.raw[:got.value]
def add_module(handle, base):
    p = ctypes.create_unicode_buffer(1024); k.GetFinalPathNameByHandleW(handle, p, 1024, 0)
    hdr = read(base, 0x400); e = struct.unpack_from('<I', hdr, 0x3c)[0]; size = struct.unpack_from('<I', read(base + e, 0x60), 0x50)[0]
    mods[base] = (p.value.replace('\\\\?\\', ''), size)
def where(a):
    for b, (n, s) in mods.items():
        if b <= a < b + s: return n, a - b
    return None
ev = ctypes.create_string_buffer(256); report = None
while True:
    if not k.WaitForDebugEvent(ev, 600000): break
    code, pid, tid = struct.unpack_from('<III', ev.raw, 0); status = 0x00010002
    if code == 3: add_module(struct.unpack_from('<Q', ev.raw, 16)[0], struct.unpack_from('<Q', ev.raw, 40)[0])
    elif code == 6:
        h, base = struct.unpack_from('<QQ', ev.raw, 16); add_module(h, base); k.CloseHandle(wt.HANDLE(h))
    elif code == 1:
        exc, = struct.unpack_from('<I', ev.raw, 16); addr, = struct.unpack_from('<Q', ev.raw, 32)
        nparams, = struct.unpack_from('<I', ev.raw, 40); info = struct.unpack_from('<15Q', ev.raw, 48); first, = struct.unpack_from('<I', ev.raw, 168)
        if exc == 0x80000003 and report is None and first: status = 0x00010002
        elif exc == 0xC0000409 or not first:
            th = k.OpenThread(0x1FFFFF, False, tid)
            raw = ctypes.create_string_buffer(1232 + 16); ctx = (ctypes.addressof(raw) + 15) & ~15
            ctypes.memmove(ctx + 0x30, struct.pack('<I', 0x100003), 4); k.GetThreadContext(th, ctypes.c_void_p(ctx))
            rsp = struct.unpack('<Q', ctypes.string_at(ctx + 0x98, 8))[0]; rip = struct.unpack('<Q', ctypes.string_at(ctx + 0xF8, 8))[0]
            stack = read(rsp, 0x8000); frames = []
            for i in range(0, len(stack) - 7, 8):
                v, = struct.unpack_from('<Q', stack, i); w = where(v)
                if w and not w[0].lower().endswith(('ntdll.dll', 'kernelbase.dll', 'kernel32.dll')): frames.append((i, w))
            report = {'exception': hex(exc), 'firstChance': first, 'fastFailCode': info[0] if nparams else None, 'address': where(addr), 'rip': where(rip), 'frames': frames[:40]}
            k.TerminateProcess(hproc, 0xC0000409)
        else: status = 0x80010001
    elif code == 5: k.ContinueDebugEvent(pid, tid, status); break
    k.ContinueDebugEvent(pid, tid, status)
sym = Path.home() / 'deco/doe/bench/out/fawn-windows-build/chromium/src/third_party/llvm-build/Release+Asserts/bin/llvm-symbolizer.exe'
def symbolize(path, rva):
    data = open(path, 'rb').read(0x400); e = struct.unpack_from('<I', data, 0x3c)[0]
    base = struct.unpack_from('<Q', open(path, 'rb').read(e + 0x40)[e + 0x30:e + 0x38])[0]
    r = subprocess.run([str(sym), f'--obj={path}', '--relative-address', hex(rva)], capture_output=True, text=True)
    return ' '.join(r.stdout.split())
if report is None: print('NO FAULT CAUGHT'); sys.exit(0)
print('exception', report['exception'], 'firstChance', report['firstChance'], 'fastFailCode', report['fastFailCode'])
for label in ('address', 'rip'):
    w = report[label]; print(label, w and (Path(w[0]).name, hex(w[1]), symbolize(w[0], w[1])))
seen = set()
for off, (path, rva) in report['frames']:
    if (path, rva) in seen: continue
    seen.add((path, rva)); print(f'  rsp+{off:#06x} {Path(path).name}+{rva:#x} {symbolize(path, rva)}')
