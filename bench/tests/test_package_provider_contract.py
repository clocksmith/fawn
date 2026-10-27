"""Exercise provider selection/provenance helpers without creating GPU devices."""
from __future__ import annotations

import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = (ROOT/'bench/executors/node-webgpu/provider-contract.js').as_uri()


def owner_function(source: str, name: str) -> str:
    match = re.search(r'^static [^\n]+\b'+name+r'\([^\n]*\) \{[\s\S]*?^\}', source, re.M)
    if not match:raise AssertionError(name)
    return match.group()


class ProviderContractTests(unittest.TestCase):
    def test_backend_filters_observed_types_and_no_fallback_import(self) -> None:
        script = '''
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { providerBackendContract, observedBackendSelection, loadPinnedDawn, pinnedDawnFiles } from MODULE;
for (const [backend, type] of Object.entries({metal:5,vulkan:6,d3d12:4})) {
 const doe=providerBackendContract(backend,'doe');
 assert.deepEqual(doe.adapterOptions,{backend});
 assert.deepEqual(doe.createOptions,[]);
 assert.equal(observedBackendSelection(doe,{backendType:type}).observedBackendType,type);
 assert.throws(()=>observedBackendSelection(doe,{}),/mismatch/);
 assert.throws(()=>observedBackendSelection(doe,{backendType:99}),/mismatch/);
 const dawn=providerBackendContract(backend,'node-webgpu');
 assert.deepEqual(dawn.createOptions,[`backend=${backend}`]);
 assert.equal(observedBackendSelection(dawn,{}).observedBackendType,null);
 assert.throws(()=>observedBackendSelection(dawn,{backendType:99}),/mismatch/);
}
assert.equal(providerBackendContract('','doe'),null);
assert.throws(()=>providerBackendContract('null','doe'),/unsupported/);
const base=join(process.argv[1],'bench/vendor/node-webgpu-package');mkdirSync(join(base,'dist'),{recursive:true});
writeFileSync(join(base,'package.json'),JSON.stringify({type:'module',version:'0.4.0'}));
writeFileSync(join(base,'index.js'),"throw new Error('exact-vendor-import-rejection');");
for(const name of ['darwin-universal','linux-x64','linux-arm64','win32-x64'])writeFileSync(join(base,'dist',`${name}.dawn.node`),'not loaded');
writeFileSync(join(base,'dist/d3dcompiler_47.dll'),'windows dependency');
assert.equal(Object.keys(pinnedDawnFiles(process.argv[1],'win32','x64').inputHashes).length,4);
assert.equal(Object.keys(pinnedDawnFiles(process.argv[1],'darwin','arm64').inputHashes).length,3);
await assert.rejects(loadPinnedDawn(process.argv[1]),/exact-vendor-import-rejection/);
writeFileSync(join(base,'package.json'),JSON.stringify({type:'module',version:'wrong'}));
assert.throws(()=>pinnedDawnFiles(process.argv[1]),/version mismatch/);
console.log('backend and strict vendored rejection controls passed');
'''.replace('from MODULE', 'from '+json.dumps(MODULE))
        with tempfile.TemporaryDirectory() as out:
            run=subprocess.run(['node','--input-type=module','-e',script,str(Path(out).resolve())],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_addon_backend_parser_and_observer_with_typed_cpu_provider(self) -> None:
        instance=(ROOT/'runtime/bridge/webgpu-addon/doe_napi_instance.c').read_text()
        caps=(ROOT/'runtime/bridge/webgpu-addon/doe_napi_caps.c').read_text()
        functions='\n'.join([owner_function(instance,'adapter_backend_type_from_name'),
            owner_function(instance,'parse_adapter_backend'),owner_function(caps,'set_observed_adapter_backend')])
        # Real helper functions, actual pinned WGPUAdapterInfo; only Node transport/provider are CPU mocks.
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "webgpu.h"
typedef void* napi_env;
typedef struct Value { int type; const char* bytes; size_t length; uint32_t number; } Value;
typedef Value* napi_value;
typedef int napi_valuetype;
enum { napi_ok=0, napi_undefined=0, napi_string=1, napi_number=2 };
static Value backend_value, observed_value;
static unsigned published, freed, queried;
static WGPUStatus provider_status;
static uint32_t provider_backend;
#define WGPU_STATUS_SUCCESS WGPUStatus_Success
static int napi_get_named_property(napi_env e,napi_value o,const char* n,napi_value* v) {(void)e;(void)o;assert(strcmp(n,"backend")==0);*v=&backend_value;return napi_ok;}
static int napi_typeof(napi_env e,napi_value v,napi_valuetype* t) {(void)e;*t=v->type;return napi_ok;}
static int napi_get_value_string_utf8(napi_env e,napi_value v,char* out,size_t size,size_t* len) {(void)e;*len=v->length;if(out){assert(size>v->length);memcpy(out,v->bytes,v->length);out[v->length]=0;}return napi_ok;}
static int napi_create_uint32(napi_env e,uint32_t n,napi_value* v) {(void)e;observed_value.number=n;*v=&observed_value;return napi_ok;}
static int napi_set_named_property(napi_env e,napi_value o,const char* n,napi_value v) {(void)e;(void)o;assert(strcmp(n,"backendType")==0);assert(v==&observed_value);published++;return napi_ok;}
static WGPUStatus query_backend(void* a,WGPUAdapterInfo* i) {assert(a==(void*)1);queried++;i->backendType=(WGPUBackendType)provider_backend;return provider_status;}
static void free_info(WGPUAdapterInfo i) {assert((uint32_t)i.backendType==provider_backend);freed++;}
static WGPUStatus (*pfn_wgpuAdapterGetInfo)(void*,WGPUAdapterInfo*)=query_backend;
static void (*pfn_wgpuAdapterInfoFreeMembers)(WGPUAdapterInfo)=free_info;
FUNCTIONS
int main(void) {
 const char* names[]={"metal","vulkan","d3d12"};const uint32_t types[]={WGPUBackendType_Metal,WGPUBackendType_Vulkan,WGPUBackendType_D3D12};
 for(unsigned i=0;i<3;i++){uint32_t out=99;backend_value=(Value){napi_string,names[i],strlen(names[i]),0};assert(parse_adapter_backend(NULL,NULL,&out));assert(out==types[i]);}
 uint32_t out=99;backend_value=(Value){napi_undefined,NULL,0,0};assert(parse_adapter_backend(NULL,NULL,&out));assert(out==99);
 backend_value=(Value){napi_string,"unknown",7,0};assert(!parse_adapter_backend(NULL,NULL,&out));assert(out==99);
 backend_value=(Value){napi_string,"metal\0tail",10,0};assert(!parse_adapter_backend(NULL,NULL,&out));assert(out==99);
 backend_value=(Value){napi_number,NULL,0,0};assert(!parse_adapter_backend(NULL,NULL,&out));
 backend_value=(Value){napi_string,"",32,0};assert(!parse_adapter_backend(NULL,NULL,&out));
 for(unsigned i=0;i<3;i++){provider_status=WGPUStatus_Success;provider_backend=types[i];set_observed_adapter_backend(NULL,NULL,(void*)1);assert(observed_value.number==types[i]);}
 assert(published==3&&freed==3&&queried==3);
 provider_status=WGPUStatus_Error;set_observed_adapter_backend(NULL,NULL,(void*)1);assert(published==3&&freed==3&&queried==4);
 pfn_wgpuAdapterGetInfo=NULL;set_observed_adapter_backend(NULL,NULL,(void*)1);assert(published==3&&queried==4);
 puts("actual addon parser and standard provider backend observation passed");
}
'''.replace('FUNCTIONS',functions)
        with tempfile.TemporaryDirectory() as out:
            source=Path(out)/'probe.c';source.write_text(code);binary=Path(out)/'probe'
            run=subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'runtime/zig/vendor/webgpu-headers'),str(source),'-o',str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        # The real request path rejects an invalid option before invoking either provider request entry.
        self.assertLess(instance.index('if (!parse_adapter_backend'),instance.index('if (pfn_doeRequestAdapterFlat)'))


if __name__=='__main__':unittest.main()
