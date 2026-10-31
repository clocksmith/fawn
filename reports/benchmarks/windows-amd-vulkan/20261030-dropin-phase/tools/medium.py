# medium.py <args for count_run.py>: run count_run.py at Medium integrity (the Vulkan loader
# ignores VK_ADD_LAYER_PATH in High-integrity processes) and wait for it.
from pathlib import Path
import ctypes, ctypes.wintypes as wt, sys
h = Path.home()
a32 = ctypes.WinDLL('advapi32', use_last_error=True); k32 = ctypes.WinDLL('kernel32', use_last_error=True)
tok = wt.HANDLE(); dup = wt.HANDLE()
k32.GetCurrentProcess.restype = wt.HANDLE
a32.OpenProcessToken.argtypes = [wt.HANDLE, wt.DWORD, ctypes.POINTER(wt.HANDLE)]
a32.SetTokenInformation.argtypes = [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]
a32.GetLengthSid.argtypes = [ctypes.c_void_p]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
assert a32.OpenProcessToken(k32.GetCurrentProcess(), 0x000F01FF, ctypes.byref(tok)), ctypes.get_last_error()
assert a32.DuplicateTokenEx(tok, 0x000F01FF, None, 2, 1, ctypes.byref(dup)), ctypes.get_last_error()
sid = ctypes.c_void_p()
assert a32.ConvertStringSidToSidW('S-1-16-8192', ctypes.byref(sid)), ctypes.get_last_error()
class SAA(ctypes.Structure): _fields_ = [('Sid', ctypes.c_void_p), ('Attributes', wt.DWORD)]
til = SAA(sid, 0x20)
assert a32.SetTokenInformation(dup, 25, ctypes.byref(til), ctypes.sizeof(til) + a32.GetLengthSid(sid)), ctypes.get_last_error()
class SI(ctypes.Structure): _fields_ = [('cb', wt.DWORD), ('r', wt.LPWSTR), ('d', wt.LPWSTR), ('t', wt.LPWSTR)] + [(f'x{i}', wt.DWORD) for i in range(8)] + [('w', wt.WORD), ('c', wt.WORD), ('p', ctypes.c_void_p), ('i', wt.HANDLE), ('o', wt.HANDLE), ('e', wt.HANDLE)]
class PI(ctypes.Structure): _fields_ = [('hp', wt.HANDLE), ('ht', wt.HANDLE), ('pid', wt.DWORD), ('tid', wt.DWORD)]
si = SI(); si.cb = ctypes.sizeof(si); pi = PI()
cmd = ctypes.create_unicode_buffer(f'"{sys.executable}" -u "{h / "wvk/count/count_run.py"}" ' + ' '.join(sys.argv[1:]))
assert a32.CreateProcessAsUserW(dup, None, cmd, None, None, False, 0x08000000, None, str(h), ctypes.byref(si), ctypes.byref(pi)), ctypes.get_last_error()
k32.WaitForSingleObject(pi.hp, 0xFFFFFFFF)
code = wt.DWORD(); k32.GetExitCodeProcess(pi.hp, ctypes.byref(code))
print('exit', code.value); print((h / 'wvk/count/progress.txt').read_text())
