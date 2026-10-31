# Background Dawn d04f88359 Vulkan build on the Windows host; log at ~/wvk/dawn-build.log
from pathlib import Path
import os,sys,subprocess,importlib.util,time
h=Path.home();d=h/'wvk/dawn';log=open(h/'wvk/dawn-build.log','a',buffering=1)
def run(cmd,cwd=None):
    log.write(f'\n$ {cmd}\n');t=time.time()
    r=subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
    log.write(f'-> rc {r.returncode} {time.time()-t:.0f}s\n')
    if r.returncode: log.write('FAILED\n');sys.exit(1)
repo=Path.home()/'deco/doe';L=Path(os.environ['LOCALAPPDATA'])
helper=repo/'bench/out/fawn-windows-build/doe/browser/chromium/scripts/build-windows.py'
spec=importlib.util.spec_from_file_location('envhelper',helper);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
os.environ.update(m.developer_environment(L/'deco/toolchains/vs2026-buildtools'))
ide=L/'deco/toolchains/vs2026-buildtools/Common7/IDE/CommonExtensions/Microsoft/CMake'
os.environ['PATH']=str(ide/'CMake/bin')+os.pathsep+str(ide/'Ninja')+os.pathsep+str(Path(sys.executable).parent)+os.pathsep+os.environ['PATH']
if not (d/'.git').exists():
    run(['git','clone','--filter=blob:none','--no-checkout','https://dawn.googlesource.com/dawn',str(d)])
run(['git','checkout','--detach','d04f88359391a12375c1a3e948a0b2dcb4c97a18'],cwd=d)
run(['git','log','-1','--format=%H %cI %s'],cwd=d)
run(['cmake','-S','.','-B','out/Release','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=cl','-DCMAKE_CXX_COMPILER=cl',
     '-DDAWN_FETCH_DEPENDENCIES=ON','-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED','-DDAWN_BUILD_SAMPLES=OFF','-DDAWN_BUILD_TESTS=OFF',
     '-DDAWN_BUILD_BENCHMARKS=OFF','-DDAWN_ENABLE_DESKTOP_GL=OFF','-DDAWN_ENABLE_OPENGLES=OFF','-DDAWN_ENABLE_SWIFTSHADER=OFF',
     '-DDAWN_USE_GLFW=OFF','-DDAWN_ENABLE_NULL=ON','-DDAWN_ENABLE_VULKAN=ON','-DDAWN_ENABLE_D3D11=OFF','-DDAWN_ENABLE_D3D12=OFF',
     '-DTINT_BUILD_TESTS=OFF','-DTINT_BUILD_CMD_TOOLS=OFF','-DTINT_BUILD_GLSL_WRITER=OFF','-DTINT_BUILD_HLSL_WRITER=OFF',
     '-DTINT_BUILD_MSL_WRITER=OFF','-DDAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON'],cwd=d)
run(['ninja','-C','out/Release','-j8','webgpu_dawn'],cwd=d)
log.write('BUILD-DONE\n')
