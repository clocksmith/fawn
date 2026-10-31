# Diagnostic /Z7 build of the pushed main tree's DLL and the four failing clients (never committed).
from pathlib import Path
import os,sys,subprocess,shutil,json,importlib.util,time
h=Path.home();lock=h/'doe-gpu-lock'
src=h/'wvk/src-dbg'
if src.exists(): shutil.rmtree(src)
shutil.copytree(h/'wvk/src-main3',src)
p=src/'runtime/c/native/tools/build_windows.py';s=p.read_text()
s=s.replace("'/MD', '/O2', '/WX',","'/MD', '/O2', '/Z7', '/WX',").replace("'/Fe' + str(library), '/link',","'/Fe' + str(library), '/link', '/DEBUG',")
s=s.replace("    if sorted(actual) != sorted(names):\n        raise ValueError","    if False:\n        raise ValueError")  # diagnostic only: /DEBUG changes the dumpbin listing
assert 'if False:' in s
p.write_text(s)
while True:
    try: os.mkdir(lock);break
    except FileExistsError: time.sleep(0.5)
try:
    r=subprocess.run([sys.executable,str(h/'wvk/build_doe.py'),'wvk/src-dbg','wvk/doe-dbg'],capture_output=True,text=True);print(r.stdout[-300:],r.stderr[-800:])
finally: os.rmdir(lock)
repo=h/'deco/doe';L=Path(os.environ['LOCALAPPDATA'])
helper=repo/'bench/out/fawn-windows-build/doe/browser/chromium/scripts/build-windows.py'
spec=importlib.util.spec_from_file_location('envhelper',helper);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
os.environ.update(m.developer_environment(L/'deco/toolchains/vs2026-buildtools'))
clang=repo/'bench/out/fawn-windows-build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe'
b=h/'wvk/doe-dbg';scope=json.loads((b/'native-scope.json').read_text())
contract=json.loads((src/'config/compute-program.schema.json').read_text())['$defs']['nativeContractVersion']['const']
flags=[str(clang),'--target=x86_64-pc-windows-msvc','/nologo','/MD','/O2','/Z7','/D_CRT_SECURE_NO_WARNINGS','/DWGPU_SHARED_LIBRARY',f'/DDOE_COMPUTE_PROGRAM_CONTRACT={contract}u','/clang:-std=c11',
 '/I'+str(src/'runtime/c/native/include'),'/I'+str(src/'runtime/zig/vendor/webgpu-headers'),'/I'+str(src/'runtime/c/compiler/include')]+['/I'+str(src/'runtime/c'/o/'include') for o in scope['owners']]
for c in ['limits','occlusion','workgroups','compute_programs']:
    r=subprocess.run(flags+[str(src/'runtime/c/native/tests'/(c+'.c')),'/Fo'+str(b/(c+'.obj')),'/Fe'+str(b/(c+'.exe')),'/link','/DEBUG',str(b/'webgpu_doe_c.lib')],capture_output=True,text=True,cwd=b)
    print(c,r.returncode,(r.stdout+r.stderr)[-300:])
print(sorted(x.name for x in b.glob('*.pdb')))
