# After dbgbuild: run each failing client under dbgrun with the GPU lock held.
from pathlib import Path
import os,sys,subprocess,time
h=Path.home();lock=h/'doe-gpu-lock';b=h/'wvk/doe-dbg'
r=subprocess.run([sys.executable,str(h/'wvk/dbg/dbgbuild.py')],capture_output=True,text=True);print(r.stdout[-3000:],r.stderr[-1500:],flush=True)
while True:
    try: os.mkdir(lock);break
    except FileExistsError: time.sleep(0.5)
try:
    env=dict(os.environ,DOE_C_TEST_ADAPTER='physical',DOE_C_TEST_BACKEND='vulkan')
    for c in ['limits','occlusion','workgroups','compute_programs']:
        r=subprocess.run([sys.executable,str(h/'wvk/dbg/dbgrun.py'),str(h/'wvk/src-dbg/runtime/c/native'),str(b/(c+'.exe'))],capture_output=True,text=True,env=env,timeout=600)
        print('=====',c,'\n',r.stdout[-6000:],r.stderr[-2000:],flush=True)
finally: os.rmdir(lock)
print('DBG-DONE')
