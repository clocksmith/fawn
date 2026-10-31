# hb.py <src> <libdir> <name>: build harness <name>.exe beside <libdir>/<dll> via the cohort tool
from pathlib import Path
import os,sys,subprocess,importlib.util
h=Path.home();src=h/sys.argv[1];libdir=h/sys.argv[2];name=sys.argv[3];dll=sys.argv[4] if len(sys.argv)>4 else 'webgpu_doe_c.dll'
repo=Path.home()/'deco/doe';L=Path(os.environ['LOCALAPPDATA'])
helper=repo/'bench/out/fawn-windows-build/doe/browser/chromium/scripts/build-windows.py'
spec=importlib.util.spec_from_file_location('envhelper',helper);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
os.environ.update(m.developer_environment(L/'deco/toolchains/vs2026-buildtools'));os.environ['PYTHONDONTWRITEBYTECODE']='1'
clang=repo/'bench/out/fawn-windows-build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe'
r=subprocess.run([sys.executable,'-m','bench.tools.dropin_phase_cohort','build','--library',str(libdir/dll),'--output',str(libdir/(name+'.exe')),'--cc',str(clang)],cwd=src,capture_output=True,text=True)
print(r.stdout[-3000:],r.stderr[-3000:])
(libdir/(name+'-build.json')).write_text(r.stdout)
