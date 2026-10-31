# Build webgpu_doe_c.dll (+ doe_compiler.dll) from ~/<src> into fresh ~/<out>. argv: src out
from pathlib import Path
import os,sys,subprocess,importlib.util,shutil,time,hashlib
src=Path.home()/sys.argv[1];out=Path.home()/sys.argv[2]
repo=Path.home()/'deco/doe';L=Path(os.environ['LOCALAPPDATA'])
helper=repo/'bench/out/fawn-windows-build/doe/browser/chromium/scripts/build-windows.py'
spec=importlib.util.spec_from_file_location('envhelper',helper);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
os.environ.update(m.developer_environment(L/'deco/toolchains/vs2026-buildtools'));os.environ['PYTHONDONTWRITEBYTECODE']='1'
clang=repo/'bench/out/fawn-windows-build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe'
dxc=L/'deco/validation/doe-d3d12-159b58-ki3pcb23/dxc'
if out.exists(): shutil.rmtree(out)
cmd=[sys.executable,str(src/'runtime/c/native/tools/build_windows.py'),'--out',str(out),'--dxc-root',str(dxc),'--clang-cl',str(clang)]
t=time.time();r=subprocess.run(cmd,cwd=src,capture_output=True,text=True,timeout=3000)
(Path.home()/'wvk'/(Path(sys.argv[2]).name+'-build.log')).write_text(r.stdout+r.stderr)
print('BUILD',r.returncode,round(time.time()-t,1))
if r.returncode: print((r.stdout+r.stderr)[-4000:]);sys.exit(1)
print('DLL',hashlib.sha256((out/'webgpu_doe_c.dll').read_bytes()).hexdigest())
print('CLANG',subprocess.run([str(clang),'--version'],capture_output=True,text=True).stdout.splitlines()[0])
