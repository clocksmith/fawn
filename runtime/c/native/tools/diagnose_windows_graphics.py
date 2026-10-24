#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Trace the unchanged public graphics client with actual D3D12 SDK diagnostics.

Requires the existing matching native DLL producer and an x64 SDK developer shell.
Runs explicit CPU WARP under a 20-second process-tree deadline; no hardware lane.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import build_windows

ROOT = build_windows.ROOT
C_ROOT = build_windows.C_ROOT
REPO = build_windows.REPO
CLIENT_GIT_LF_SHA256 = '59ec673c671dde4de3cd4e06f7fc4252f4fde4b75edb5a980d044ea5a919538e'
RUNTIME_FILES = ('webgpu_doe_c.dll', 'doe_compiler.dll', 'dxcompiler.dll', 'dxil.dll')

WRAPPER = r'''/* SPDX-License-Identifier: MIT
 * Actual SDK diagnostics over the unchanged source-bound public client. */
#include @NATIVE_INTERNAL@
#include @PRIVATE_INTERNAL@
#include @WINDOWS_INTERNAL@
#include <d3d12sdklayers.h>
#include <inttypes.h>
#include <stdio.h>

static unsigned trace_phase;

static ID3D12InfoQueue *trace_queue(WGPUDevice handle)
{
    native_device *device=(native_device *)handle;
    if (!device || device->backend.kind!=WGPUBackendType_D3D12 ||
        !device->backend.value.d3d12.context || !device->backend.value.d3d12.context->native ||
        !device->backend.value.d3d12.context->native->device) {
        fprintf(stderr,"INFO_QUEUE unavailable: no selected D3D12 device\n");
        return NULL;
    }
    ID3D12InfoQueue *queue=NULL;
    HRESULT result=ID3D12Device_QueryInterface(device->backend.value.d3d12.context->native->device,
        &IID_ID3D12InfoQueue,(void **)&queue);
    fprintf(stderr,"INFO_QUEUE QueryInterface=%" PRId32 " available=%u\n",(int32_t)result,(unsigned)(queue!=NULL));
    return SUCCEEDED(result) ? queue : NULL;
}

static void trace_messages(ID3D12InfoQueue *queue)
{
    if (!queue) return;
    UINT64 count=ID3D12InfoQueue_GetNumStoredMessagesAllowedByRetrievalFilter(queue);
    fprintf(stderr,"INFO_QUEUE stored=%" PRIu64 "\n",(uint64_t)count);
    UINT64 limit=count<64 ? count : 64;
    for (UINT64 i=0;i<limit;++i) {
        SIZE_T bytes=0;
        HRESULT result=ID3D12InfoQueue_GetMessage(queue,i,NULL,&bytes);
        if (FAILED(result) || bytes<sizeof(D3D12_MESSAGE) || bytes>65536) {
            fprintf(stderr,"INFO_QUEUE message=%" PRIu64 " size_status=%" PRId32 " bytes=%zu\n",
                (uint64_t)i,(int32_t)result,(size_t)bytes);
            continue;
        }
        SIZE_T capacity=bytes;
        D3D12_MESSAGE *message=malloc(capacity);
        if (!message) { fprintf(stderr,"INFO_QUEUE diagnostic allocation failed\n"); break; }
        result=ID3D12InfoQueue_GetMessage(queue,i,message,&bytes);
        if (SUCCEEDED(result)) {
            uintptr_t base=(uintptr_t)message,description=(uintptr_t)message->pDescription;
            SIZE_T length=message->DescriptionByteLength;
            fprintf(stderr,"INFO_QUEUE message=%" PRIu64 " category=%u severity=%u id=%u description=",
                (uint64_t)i,(unsigned)message->Category,(unsigned)message->Severity,(unsigned)message->ID);
            if (description>=base && description-base<=capacity && length<=capacity-(description-base)) {
                if (length && message->pDescription[length-1]==0) --length;
                if (length) fwrite(message->pDescription,1,length,stderr);
            } else fprintf(stderr,"[description outside returned SDK buffer]");
            fputc('\n',stderr);
        } else fprintf(stderr,"INFO_QUEUE message=%" PRIu64 " get_status=%" PRId32 "\n",
            (uint64_t)i,(int32_t)result);
        free(message);
    }
    if (count>limit) fprintf(stderr,"INFO_QUEUE messages_truncated=%" PRIu64 "\n",(uint64_t)(count-limit));
}

/* Defined before the macro: this call uses the real imported public API. */
static WGPURenderPipeline trace_pipeline(WGPUDevice device,const WGPURenderPipelineDescriptor *desc)
{
    static const char *const names[]={"yellow-ccw","yellow-cw","sampled-ccw"};
    unsigned phase=trace_phase++;
    const char *name=phase<3 ? names[phase] : "additional";
    fprintf(stderr,"PIPELINE_BEGIN phase=%u name=%s\n",phase+1,name);
    ID3D12InfoQueue *queue=trace_queue(device);
    if (queue) ID3D12InfoQueue_ClearStoredMessages(queue);
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(device,desc);
    fprintf(stderr,"PIPELINE_END phase=%u name=%s returned=%u\n",phase+1,name,(unsigned)(result!=NULL));
    trace_messages(queue);
    if (queue) ID3D12InfoQueue_Release(queue);
    return result;
}

#define wgpuDeviceCreateRenderPipeline trace_pipeline
#define main original_graphics_client_main
#include @CLIENT@
#undef main
#undef wgpuDeviceCreateRenderPipeline

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    setvbuf(stderr,NULL,_IONBF,0);
    fprintf(stderr,"SCOPE unchanged source-bound graphics client on explicit CPU WARP; no physical GPU\n");
    ID3D12Debug *debug=NULL;
    HRESULT result=D3D12GetDebugInterface(&IID_ID3D12Debug,(void **)&debug);
    fprintf(stderr,"DEBUG_LAYER query=%" PRId32 " available=%u\n",(int32_t)result,(unsigned)(debug!=NULL));
    if (SUCCEEDED(result) && debug) {
        ID3D12Debug_EnableDebugLayer(debug);
        fprintf(stderr,"DEBUG_LAYER EnableDebugLayer returned before first device creation\n");
    } else fprintf(stderr,"DEBUG_LAYER unavailable; client still runs, no debug-message evidence promised\n");
    if (debug) ID3D12Debug_Release(debug);
    return original_graphics_client_main();
}
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wrapper_source() -> str:
    substitutions = {
        '@NATIVE_INTERNAL@': ROOT / 'src/internal.h',
        '@PRIVATE_INTERNAL@': C_ROOT / 'backends/d3d12/src/internal.h',
        '@WINDOWS_INTERNAL@': C_ROOT / 'backends/d3d12/src/windows_internal.h',
        '@CLIENT@': ROOT / 'tests/d3d12_graphics.c',
    }
    result = WRAPPER
    for token, path in substitutions.items():
        result = result.replace(token, json.dumps(path.resolve().as_posix()))
    return result


def source_inputs(owners: list[Path]) -> list[Path]:
    headers = set(ROOT.joinpath('src').glob('*.h')) | set(ROOT.joinpath('include').glob('*.h'))
    headers |= set(C_ROOT.joinpath('compiler/include').glob('*.h'))
    headers |= set(C_ROOT.joinpath('common/include').glob('*.h'))
    for owner in owners:
        headers |= set(owner.joinpath('include').glob('*.h')) | set(owner.joinpath('src').glob('*.h'))
    return sorted(headers) + [ROOT / 'sources.mk', REPO / 'runtime/zig/vendor/webgpu-headers/webgpu.h',
                              ROOT / 'tests/d3d12_graphics.c', ROOT / 'tests/consumer.h',
                              ROOT / 'tests/adapter_class.h', Path(__file__).resolve(),
                              ROOT / 'tools/build_windows.py']


def run_logged(command: list[str], out: Path, name: str, records: list[dict]) -> int:
    record = {'argv': command, 'stdout': name + '.stdout.txt', 'stderr': name + '.stderr.txt'}
    records.append(record)
    with out.joinpath(record['stdout']).open('wb') as stdout, out.joinpath(record['stderr']).open('wb') as stderr:
        process = subprocess.run(command, cwd=out, stdout=stdout, stderr=stderr, timeout=120)
    record['exit'] = process.returncode
    return process.returncode


def bounded_client(executable: Path, out: Path, record: dict) -> dict:
    environment = os.environ.copy()
    environment.update(DOE_C_TEST_BACKEND='d3d12', DOE_C_TEST_ADAPTER='software')
    record.update({'argv': [str(executable)], 'deadline_seconds': 20,
              'environment': {'DOE_C_TEST_BACKEND': 'd3d12', 'DOE_C_TEST_ADAPTER': 'software'},
              'stdout': 'client.stdout.txt', 'stderr': 'client.stderr.txt', 'timed_out': False})
    with out.joinpath(record['stdout']).open('wb') as stdout, out.joinpath(record['stderr']).open('wb') as stderr:
        process = subprocess.Popen([str(executable)], cwd=out, env=environment, stdout=stdout, stderr=stderr,
                                   creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        try:
            record['exit'] = process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            record['timed_out'] = True
            kill_command = ['taskkill.exe', '/PID', str(process.pid), '/T', '/F']
            record['taskkill'] = {'argv': kill_command}
            try:
                killed = subprocess.run(kill_command, capture_output=True, timeout=10)
                out.joinpath('taskkill.stdout.txt').write_bytes(killed.stdout)
                out.joinpath('taskkill.stderr.txt').write_bytes(killed.stderr)
                record['taskkill']['exit'] = killed.returncode
                if killed.returncode:
                    record['process_tree_cleanup_unproven'] = True
            except Exception as error:
                record['taskkill']['failure'] = {'type': type(error).__name__, 'message': str(error)}
                record['process_tree_cleanup_unproven'] = True
            try:
                record['exit'] = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                record['exit'] = process.wait(timeout=10)
                record['process_tree_cleanup_unproven'] = True
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--native-dir', type=Path, required=True, help='matching existing native SDK DLL producer')
    parser.add_argument('--clang-cl', default='clang-cl')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires actual Windows x64 SDK; no replacement API or device is used on other hosts')
    out, producer = args.out.resolve(), args.native_dir.resolve(strict=True)
    if out == REPO or REPO in out.parents or out == producer or producer in out.parents:
        parser.error('out must be separate from both source checkout and immutable producer directory')
    if out.exists() and any(out.iterdir()):
        parser.error('out must be fresh or empty; previous diagnostics will not be overwritten')
    compiler = shutil.which(args.clang_cl)
    if not compiler:
        parser.error('actual clang-cl is required; no toolchain fallback')
    owners = build_windows.owner_paths()
    inputs = source_inputs(owners)
    source_before = {path.relative_to(REPO).as_posix(): digest(path) for path in inputs}
    client_bytes = ROOT.joinpath('tests/d3d12_graphics.c').read_bytes()
    client_fixture = {
        'raw_sha256': hashlib.sha256(client_bytes).hexdigest(),
        'git_lf_sha256': hashlib.sha256(client_bytes.replace(b'\r\n', b'\n')).hexdigest(),
        'expected_git_lf_sha256': CLIENT_GIT_LF_SHA256,
        'comparison': 'Only CRLF-to-LF is normalized to compare checkout bytes with the Git LF blob; '
                      'all producer/header identity checks retain exact raw bytes.',
    }
    if client_fixture['git_lf_sha256'] != CLIENT_GIT_LF_SHA256:
        parser.error('public graphics client differs from the unchanged Git LF fixture')
    scope_path = producer / 'native-scope.json'
    scope = json.loads(scope_path.read_text(encoding='utf-8'))
    for name, expected in scope['input_sha256'].items():
        if name in source_before and source_before[name] != expected:
            parser.error(f'producer/private source layout mismatch: {name}')
    for path in inputs:
        name = path.relative_to(REPO).as_posix()
        if path.suffix == '.h' and name not in scope['input_sha256'] and path.parent != ROOT / 'tests':
            parser.error(f'private/header layout is not bound by native producer: {name}')
    artifacts = [producer / name for name in (*RUNTIME_FILES, 'webgpu_doe_c.lib', 'native-scope.json')]
    producer_before = {path.name: digest(path) for path in artifacts}
    if producer_before['webgpu_doe_c.dll'] != scope['native_dll_sha256']:
        parser.error('native DLL differs from producer receipt')
    for name in RUNTIME_FILES[1:]:
        if producer_before[name] != scope['packaged_runtime_sha256'][name]:
            parser.error(f'packaged dependency differs from producer receipt: {name}')
    out.mkdir(parents=True, exist_ok=True)
    records: list[dict] = []
    result = {'scope': 'Actual SDK debug-layer/InfoQueue diagnostics over unchanged public CPU WARP graphics; '
                       'client may execute software draws if pipeline construction succeeds; no physical/performance qualification.',
              'sources_before': source_before, 'producer_before': producer_before, 'commands': records,
              'client_fixture': client_fixture,
              'windows_sdk_version': os.environ.get('WindowsSDKVersion'),
              'vc_tools_version': os.environ.get('VCToolsVersion')}
    code = 1
    try:
        for path in artifacts:
            shutil.copy2(path, out / ('producer-native-scope.json' if path.name == 'native-scope.json' else path.name))
        source = out / 'graphics_diagnostic.c'
        source.write_text(wrapper_source(), encoding='utf-8')
        result['generated_source_sha256'] = digest(source)
        includes = [ROOT / 'include', REPO / 'runtime/zig/vendor/webgpu-headers', C_ROOT / 'compiler/include']
        includes.extend(owner / 'include' for owner in owners)
        executable = out / 'graphics_diagnostic.exe'
        command = [compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX',
                   '/D_CRT_SECURE_NO_WARNINGS', '/DWGPU_SHARED_LIBRARY', '/clang:-std=c11',
                   '/clang:-Wall', '/clang:-Wextra', '/clang:-Wpedantic',
                   *['/I' + str(path) for path in includes], str(source),
                   '/Fo' + str(out / 'graphics_diagnostic.obj'), '/Fe' + str(executable), '/link',
                   str(out / 'webgpu_doe_c.lib'), 'd3d12.lib', 'dxguid.lib', 'kernel32.lib', '/stack:8388608']
        result['clang_cl_sha256'] = digest(Path(compiler))
        if run_logged([compiler, '--version'], out, 'toolchain', records):
            raise RuntimeError('actual clang-cl version command failed; retain first toolchain logs')
        if run_logged(command, out, 'build', records):
            raise RuntimeError('actual SDK diagnostic build failed; retain first build logs')
        execution_paths = [source, executable, *[out / name for name in RUNTIME_FILES], out / 'webgpu_doe_c.lib']
        result['execution_artifacts_before'] = {path.name: digest(path) for path in execution_paths}
        if any(result['execution_artifacts_before'][name] != producer_before[name] for name in RUNTIME_FILES):
            raise RuntimeError('runtime copy differs from the bound producer before execution')
        if result['execution_artifacts_before'][source.name] != result['generated_source_sha256']:
            raise RuntimeError('generated diagnostic source changed during compilation')
        out.joinpath('BEFORE_EXECUTION.json').write_text(json.dumps({
            'artifacts': result['execution_artifacts_before'], 'sources': source_before,
            'producer': producer_before, 'client_fixture': client_fixture,
        }, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        result['client'] = {}
        bounded_client(executable, out, result['client'])
        code = 0 if result['client']['exit'] == 0 and not result['client']['timed_out'] else 1
    except Exception as error:
        result['failure'] = {'type': type(error).__name__, 'message': str(error)}
    finally:
        result['sources_after'] = {path.relative_to(REPO).as_posix(): digest(path) for path in inputs}
        result['producer_after'] = {path.name: digest(path) for path in artifacts}
        result['inputs_unchanged'] = source_before == result['sources_after'] and producer_before == result['producer_after']
        result['copied_runtime_after'] = {name: digest(out / name) for name in RUNTIME_FILES if (out / name).is_file()}
        result['copied_runtime_unchanged'] = result['copied_runtime_after'] == {
            name: producer_before[name] for name in RUNTIME_FILES}
        if not result['inputs_unchanged'] or not result['copied_runtime_unchanged']:
            code = 1
        if 'execution_artifacts_before' in result:
            result['execution_artifacts_after'] = {
                name: digest(out / name) for name in result['execution_artifacts_before'] if (out / name).is_file()}
            result['execution_artifacts_unchanged'] = result['execution_artifacts_before'] == result['execution_artifacts_after']
            if not result['execution_artifacts_unchanged']:
                code = 1
        result['exit'] = code
        out.joinpath('RESULTS.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2, sort_keys=True), flush=True)
    return code


if __name__ == '__main__':
    raise SystemExit(main())
