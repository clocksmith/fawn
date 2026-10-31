"""Windows Vulkan physical gates under the shared GPU lock (held for the whole run).

wingates.py <label> <build-landed:0|1> <variant=build-dir rel home>...
Compiles the public-ABI PHYSICAL_CLIENTS (clients.json) with clang-cl against each
variant's webgpu_doe_c.lib and runs them with DOE_C_TEST_ADAPTER=physical and
DOE_C_TEST_BACKEND=vulkan. Optionally builds wvk/src-landed into wvk/doe-landed first.
"""
from pathlib import Path
import os, sys, subprocess, importlib.util, json, shutil, time, hashlib
from concurrent.futures import ThreadPoolExecutor
h = Path.home(); label = sys.argv[1]; builds = [b.split(':') for b in sys.argv[2].split(',')] if sys.argv[2] != '0' else []
variants = dict(a.split('=', 1) for a in sys.argv[3:])
lock = h / 'doe-gpu-lock'; out = h / 'wvk/gates' / label
while True:
    try: os.mkdir(lock); break
    except FileExistsError: time.sleep(0.2)
try:
    out.mkdir(parents=True, exist_ok=False)
    log = open(out / 'log.txt', 'a', buffering=1)
    repo = h / 'deco/doe'; L = Path(os.environ['LOCALAPPDATA'])
    helper = repo / 'bench/out/fawn-windows-build/doe/browser/chromium/scripts/build-windows.py'
    spec = importlib.util.spec_from_file_location('envhelper', helper); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    os.environ.update(m.developer_environment(L / 'deco/toolchains/vs2026-buildtools'))
    clang = repo / 'bench/out/fawn-windows-build/chromium/src/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe'
    for bsrc, bout in builds:
        r = subprocess.run([sys.executable, str(h / 'wvk/build_doe.py'), bsrc, bout], capture_output=True, text=True)
        log.write(f'build {bout} ' + r.stdout + r.stderr[-2000:] + '\n')
        if r.returncode: sys.exit(f'{bout} build failed')
    clients = json.loads((h / 'wvk/clients.json').read_text())
    results = {}
    for name, rel in variants.items():
        build = h / rel; src = h / rel.replace('doe-', 'src-'); vo = out / name; vo.mkdir()
        scope = json.loads((build / 'native-scope.json').read_text())
        contract = json.loads((src / 'config/compute-program.schema.json').read_text())['$defs']['nativeContractVersion']['const']
        flags = [str(clang), '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/D_CRT_SECURE_NO_WARNINGS', '/DWGPU_SHARED_LIBRARY',
                 f'/DDOE_COMPUTE_PROGRAM_CONTRACT={contract}u', '/clang:-std=c11',
                 '/I' + str(src / 'runtime/c/native/include'), '/I' + str(src / 'runtime/zig/vendor/webgpu-headers'), '/I' + str(src / 'runtime/c/compiler/include')]
        flags += ['/I' + str(src / 'runtime/c' / o / 'include') for o in scope['owners']]
        for f in build.glob('*.dll'): shutil.copy2(f, vo / f.name)
        def compile_one(item):
            client, srcs = item
            r = subprocess.run(flags + [str(src / 'runtime/c/native' / s) for s in srcs] + ['/Fo' + str(vo / (client + '.obj')), '/Fe' + str(vo / (client + '.exe')),
                               '/link', str(build / 'webgpu_doe_c.lib')], capture_output=True, text=True, cwd=vo)
            return client, r.returncode, (r.stdout + r.stderr)[-600:]
        with ThreadPoolExecutor(8) as pool: built = list(pool.map(compile_one, clients))
        results[name] = {'dllSha256': hashlib.sha256((build / 'webgpu_doe_c.dll').read_bytes()).hexdigest(), 'clients': {}}
        for client, code, text in built:
            results[name]['clients'][client] = {'build': code, 'buildLog': text if code else ''}
    env = dict(os.environ, DOE_C_TEST_ADAPTER='physical', DOE_C_TEST_BACKEND='vulkan')
    for client, _ in clients:
        for name in variants:
            row = results[name]['clients'][client]
            if row['build']: continue
            t = time.time()
            try:
                # Clients open fixtures relative to runtime/c/native; their DLLs sit beside the exe.
                csrc = h / variants[name].replace('doe-', 'src-')
                r = subprocess.run([str(out / name / (client + '.exe'))], cwd=csrc / 'runtime/c/native', env=env, capture_output=True, text=True, timeout=240)
                row.update(exit=r.returncode, tail=(r.stdout + r.stderr).strip()[-400:])
            except subprocess.TimeoutExpired:
                row.update(exit='timeout', tail='')
            row['seconds'] = round(time.time() - t, 2)
            log.write(f"{client} {name} {row['exit']}\n")
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    log.write('DONE\n')
finally:
    os.rmdir(lock)
