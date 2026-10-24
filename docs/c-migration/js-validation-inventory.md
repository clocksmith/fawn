# doe-gpu JavaScript validation inventory

Evidence for an owner decision, recorded without behaviour changes; [Creation routing](#creation-routing-applied-after-the-inventory) describes the behaviour applied since. Source: `packages/doe-gpu/src/vendor/webgpu/index.js` and `shared/*.js` (browser shims and `bun-ffi.js` excluded) at c5f00ae3b. Each row is one literal `throw` or `failValidation(...)` site, plus rows for helper call sites whose bounds change the class (integer minimums of 1).

## Creation routing (applied after the inventory)

The inventory rows keep their c5f00ae3b line numbers. Since then, every synchronous creator follows the createBuffer rule: with an error scope or an `uncapturederror` handler or listener, a class-b failure reports one validation error and returns an invalid object of the right class; with neither, it throws as before. This covers createBuffer, createTexture, GPUTexture.createView, createSampler, createBindGroupLayout, createBindGroup, createPipelineLayout, createShaderModule, createQuerySet, createRenderBundleEncoder, the synchronous createComputePipeline/createRenderPipeline and getBindGroupLayout. The async pipeline creators still reject with GPUPipelineError and report nothing.

- A provider that returns NULL makes the addon return `null`; it no longer throws, and it no longer wraps pipeline creation in an internal error scope.
- Error routing has one ordered owner, the package. It pushes no native error scopes; every provider error reaches the device's uncaptured-error callback, which the addon appends to a per-device log (`deviceTakeErrors`) and counts in a shared sequence JS reads without a native call (`providerErrorSequence`). Before any package error is delivered, and at every pushErrorScope/popErrorScope, the logged provider errors are delivered first, so each scope keeps the first error in call order and scope depth is unlimited. A creation reads the sequence before and after the native call (no native call on success): provider errors reported during it are delivered once and make the returned object invalid; a NULL result with no report (the Zig drop-in for some createView failures) gets one package error. Package checks run before the native call, so they never double-report on C and still cover what the Zig drop-in accepts.
- finish() of an encoder or render bundle encoder collects the errors reported while it runs and reports at most one (the first, else the invalid-object use); it never throws. A lost device discards errors. WebIDL conversion failures (a missing descriptor or required member, an argument of the wrong type) throw TypeError even inside a scope.
- createTexture reports descriptor failures as `validation` (including sampleCount and usage checks made after normalization); only a provider's own out-of-memory report is `out-of-memory`.
- Using an invalid object is a validation error: as a descriptor member of another creation, in queue.writeBuffer/writeTexture, in a submitted command buffer, and in encoder commands. An encoder command that names one records nothing and invalidates the encoder; later binding, draw and dispatch commands record nothing; finish() reports the error and returns an invalid command buffer. Without a scope or listener the use itself throws at once.
- A lost device (the `resource-lifecycle.js:89` row) is inert: creation returns invalid objects, queue and encoder calls do nothing, popErrorScope resolves null and mapAsync rejects with AbortError; nothing throws or reports. uncapturederror fires from a queued task, so a listener never runs inside a GPU call.
- createQuerySet accepts `count` 0 (the `full-surface.js:1763` row) and reports a count above 4096 as a validation error; createRenderBundleEncoder reports a descriptor with neither a color format nor a depthStencilFormat.
- Doe capability gaps on these paths (externalTexture on the headless surface, missing binding reflection for `layout: "auto"`, a build without render bundles) are class c and still throw.
- Evidence: `packages/doe-gpu/test/integration/test-integration-creation-errors.js`.
- Encoder state follows the same rule (the `index.js:789`, `:996`, `:1274`, `:1277`, `:1423`, `:1426`, `:1663` and `:1708` rows). A command on an ended pass or a finished encoder, including `finish()` and `end()` again, reports one validation error at once and records nothing. A command on an encoder with an open pass (locked), or a `beginRenderPass` with no attachment, invalidates the encoder, so `finish()` reports the error and returns an invalid command buffer. Without a scope or listener the command throws, so the `test-integration-gpu-namespace.js:583-593` throw still holds. Evidence: `packages/doe-gpu/test/integration/test-integration-encoder-open-state.js`.

Classes: **a** the specification throws or rejects (WebIDL conversion, content-timeline TypeError/RangeError/OperationError, promise rejection); **b** the specification generates a validation error and returns an invalid object or does nothing; **c** Doe-only API, provider/surface capability, or behaviour the specification does not define (including places where WebIDL would convert silently instead of throwing).

## Summary

| class | rows |
|---|---|
| a | 49 |
| b | 45 |
| c | 58 |
| helper definition | 1 |
| total | 153 |

Cross-cutting behaviour that applies to many rows:

- `failValidation` throws a plain `Error`, so even class-a rows do not throw the WebIDL type the specification names (TypeError, OperationError, RangeError) except where noted.
- `createBuffer` and `createTexture` already route class-b failures to an error scope or `uncapturederror` when one is registered (`shouldRouteDeviceError`, `full-surface.js:196`) and throw only otherwise; `createTexture` routes every failure as `out-of-memory`. Deferred encoder commands (`t: 99`) are routed at submit the same way (`index.js:865`).
- `assertLiveResource` (about 140 call sites across GPU methods) throws for destroyed objects. For a destroyed device the specification makes calls no-ops without an error; for destroyed buffers and textures most uses are validation errors (class b).
- Below the JS layer, the addon itself throws when a native creator returns NULL (for example `createSampler failed`, `createBuffer failed`, `textureCreateView failed`), after the provider has already reported the error to the scope on C. For compute and render pipelines on C, the addon wraps the call in its own internal validation scope (`doe_scoped_validation_begin`, `doe_napi_pipeline.c`) to build the thrown message, so the provider error never reaches a user scope.

## Provider behaviour when JS lets a class-b call through

Measured on Jupiter, Radeon 8060S (RADV), by calling the addon directly with a native validation scope pushed (`addon.devicePushErrorScope`/`devicePopErrorScope`), bypassing the JS checks; C = runtime/c native library, Zig = ReleaseFast drop-in at 477901ceb.

| case | C scope | C addon | Zig scope | Zig addon |
|---|---|---|---|---|
| writeBuffer offset 8 + 16 bytes into a 16-byte buffer | "queue buffer write failed" | returns | "wgpuQueueWriteBuffer range exceeds buffer size" | returns |
| createComputePipeline with an invalid module | none (captured by the addon) | throws | "createComputePipeline: ... invalid shader module ..." | throws |
| createSampler lodMinClamp 5 > lodMaxClamp 1 | "sampler descriptor invalid" | throws | none | sampler created |
| createBuffer usage MAP_READ \| STORAGE | "buffer descriptor validation failed" | throws | none | buffer created |
| createBuffer usage 0 | "buffer descriptor validation failed" | throws | none | buffer created |
| createView format not in viewFormats | "texture view creation failed or unsupported" | throws | none | view created |

So letting class-b calls through today would surface C errors in scopes (except pipelines, until the addon stops capturing them), while the Zig drop-in silently accepts several invalid descriptors that JS currently rejects.

## Inventory

| file:line | API | condition | class | spec section | provider reports it (C/Zig) |
|---|---|---|---|---|---|
| `index.js:118` | GPUQueue.writeTexture / copies | unknown aspect string | a | [#enumdef-gputextureaspect](https://gpuweb.github.io/gpuweb/#enumdef-gputextureaspect) (WebIDL enum: TypeError) | - |
| `index.js:339` | package load | native addon unavailable | c | - | - |
| `index.js:353` | package load | runtime library unavailable | c | - | - |
| `index.js:541` | createComputePipeline/createRenderPipeline (layout "auto") | provider lacks binding reflection | c | [#abstract-opdef-default-pipeline-layout](https://gpuweb.github.io/gpuweb/#abstract-opdef-default-pipeline-layout) (always derivable) | - |
| `index.js:555` | layout "auto" | reflection read failed | c | [#abstract-opdef-default-pipeline-layout](https://gpuweb.github.io/gpuweb/#abstract-opdef-default-pipeline-layout) | - |
| `index.js:639` | GPUDevice.lost | rethrow of registration failure | c | - | - |
| `index.js:685` | GPUDevice.onuncapturederror | non-function handler | c | HTML EventHandler: non-callable becomes null, no throw | - |
| `index.js:691` | GPUDevice.onuncapturederror | addon lacks callback support | c | - | - |
| `index.js:703` | GPUDevice.onuncapturederror | addon lacks callback support | c | - | - |
| `index.js:708` | GPUDevice.onuncapturederror | addon lacks callback support | c | - | - |
| `index.js:712` | GPUDevice.onuncapturederror | rethrow | c | - | - |
| `index.js:722` | GPUDevice.pushErrorScope | unknown filter | a | [#dom-gpudevice-pusherrorscope](https://gpuweb.github.io/gpuweb/#dom-gpudevice-pusherrorscope) (GPUErrorFilter enum: TypeError) | - |
| `index.js:725` | GPUDevice.pushErrorScope | addon lacks scopes | c | - | - |
| `index.js:731` | GPUDevice.pushErrorScope | addon lacks scopes | c | - | - |
| `index.js:733` | GPUDevice.pushErrorScope | rethrow | c | - | - |
| `index.js:740` | GPUDevice.popErrorScope | addon lacks scopes | c | - | - |
| `index.js:746` | GPUDevice.popErrorScope | addon lacks scopes | c | - | - |
| `index.js:748` | GPUDevice.popErrorScope | rethrow (spec: empty stack rejects OperationError) | a | [#dom-gpudevice-poperrorscope](https://gpuweb.github.io/gpuweb/#dom-gpudevice-poperrorscope) | - |
| `index.js:789` | GPUCommandEncoder.* | command recorded on a finished encoder | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) ("ended": validation error) | not run |
| `index.js:873` | GPUQueue.submit | deferred encoder validation failure without a validation scope | b | [#dom-gpuqueue-submit](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-submit) / #dom-gpucommandencoder-finish | JS routes it to a scope when one exists; throws otherwise |
| `index.js:996` | GPUCommandEncoder.* (locked) | command while a pass is open | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) ("locked": encoder made invalid) | not run |
| `index.js:1018` | GPUQueue.submit | command buffer already submitted | b | [#dom-gpuqueue-submit](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-submit) (validation error, buffers invalidated) | C: yes (c5f00ae3b); Zig: yes (a056b8d7b) |
| `index.js:1274` | GPUComputePassEncoder.* | pass already ended | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) | not run |
| `index.js:1277` | GPUComputePassEncoder.* | parent encoder finished | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) | not run |
| `index.js:1312` | GPUComputePassEncoder.dispatchWorkgroups | no pipeline set | b | [#dom-gpucomputepassencoder-dispatchworkgroups](https://gpuweb.github.io/gpuweb/#dom-gpucomputepassencoder-dispatchworkgroups) (pass invalid; error at finish) | not run |
| `index.js:1423` | GPURenderPassEncoder.* | pass already ended | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) | not run |
| `index.js:1426` | GPURenderPassEncoder.* | parent encoder finished | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) | not run |
| `index.js:1490` | GPURenderPassEncoder.drawIndirect | addon lacks the entry | c | - | - |
| `index.js:1496` | GPURenderPassEncoder.drawIndexedIndirect | addon lacks the entry | c | - | - |
| `index.js:1615` | GPURenderBundleEncoder.drawIndirect | addon lacks the entry | c | - | - |
| `index.js:1621` | GPURenderBundleEncoder.drawIndexedIndirect | addon lacks the entry | c | - | - |
| `index.js:1663` | GPUCommandEncoder.* | encoder already finished | b | [#abstract-opdef-validate-the-encoder-state](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state) | not run |
| `index.js:1708` | GPUCommandEncoder.beginRenderPass | no color or depth/stencil attachment | b | [#abstract-opdef-gpurenderpassdescriptor-valid-usage](https://gpuweb.github.io/gpuweb/#abstract-opdef-gpurenderpassdescriptor-valid-usage) | not run |
| `index.js:1784` | GPUCommandEncoder.copyBufferToBuffer | range/usage/alignment/alias failure (unbatched path; batched path defers to submit) | b | [#dom-gpucommandencoder-copybuffertobuffer](https://gpuweb.github.io/gpuweb/#dom-gpucommandencoder-copybuffertobuffer) | not run |
| `index.js:1967` | GPUBuffer._mapReadCopyUnmap (Doe) | mode is not MAP_READ | c | - | - |
| `index.js:2223` | GPUQueue.submit | element is not a GPUCommandBuffer | a | [#dom-gpuqueue-submit](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-submit) (WebIDL sequence<GPUCommandBuffer>: TypeError) | - |
| `index.js:2291` | GPUQueue.submit | element is not a GPUCommandBuffer | a | [#dom-gpuqueue-submit](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-submit) (WebIDL: TypeError) | - |
| `index.js:2298` | GPUQueue.submit | element is not a GPUCommandBuffer | a | [#dom-gpuqueue-submit](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-submit) (WebIDL: TypeError) | - |
| `index.js:2356` | GPUQueue.__doeWriteBufferBatch (Doe) | entry exceeds batch ABI | c | - | - |
| `index.js:2361` | GPUQueue.__doeWriteBufferBatch (Doe) | batch exceeds package limit | c | - | - |
| `index.js:2426` | GPUQueue.onSubmittedWorkDone | rethrow | c | - | - |
| `index.js:2544` | GPUDevice.createShaderModule | addon threw (no native module returned) | b | [#dom-gpudevice-createshadermodule](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createshadermodule) (invalid module + validation error) | C: yes; Zig: yes (977d71538); provider returns an invalid module, so this throw is reached only when the addon itself fails |
| `index.js:2559` | GPUDevice.createComputePipeline | addon threw (native returned no pipeline) | b | [#dom-gpudevice-createcomputepipeline](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createcomputepipeline) | C: reports, but the addon's internal scope (doe_scoped_validation_begin) captures it for the thrown message; Zig: yes (477901ceb) and the addon also throws |
| `index.js:2564` | GPUDevice.createBindGroupLayout | externalTexture entry (headless) | c | - | - |
| `index.js:2586` | GPUDevice.createBindGroup | externalTexture resource (headless) | c | - | - |
| `index.js:2729` | GPUDevice.createRenderPipeline | addon threw (native returned no pipeline) | b | [#dom-gpudevice-createrenderpipeline](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createrenderpipeline) | C: captured by the addon's internal scope as for compute; Zig: yes |
| `index.js:2761` | GPUDevice.pushErrorScope | rethrow | c | - | - |
| `index.js:2774` | GPUDevice.popErrorScope | rethrow | c | - | - |
| `index.js:2810` | compute program (Doe) | no native recording | c | - | - |
| `index.js:2822` | compute program (Doe) | rethrow | c | - | - |
| `index.js:2885` | createNativeDirect (Doe) | surface unavailable | c | - | - |
| `index.js:2897` | createCanvasContext (Doe) | Metal canvas unavailable | c | - | - |
| `index.js:2909` | setNativeTimeoutMs (Doe) | addon lacks entry | c | - | - |
| `shared/compiler-errors.js:16` | GPUDevice.createShaderModule (shader check) | native WGSL check failed | b | [#dom-gpudevice-createshadermodule](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createshadermodule) | C/Zig: yes (module path) |
| `shared/encoder-surface.js:22` | setImmediates | data is not a BufferSource | a | immediates: WebIDL AllowSharedBufferSource (TypeError) | - |
| `shared/encoder-surface.js:33` | setImmediates | dataOffset/size exceed data | a | immediates: content-timeline OperationError | - |
| `shared/encoder-surface.js:691` | GPUCommandEncoder.writeTimestamp | queryIndex >= count | c | not in core WebGPU (encoder-level writeTimestamp removed); Dawn treats as validation error | - |
| `shared/encoder-surface.js:702` | GPUCommandEncoder.resolveQuerySet | firstQuery+queryCount > count | b | [#dom-gpucommandencoder-resolvequeryset](https://gpuweb.github.io/gpuweb/#dom-gpucommandencoder-resolvequeryset) | not run |
| `shared/full-surface.js:49` | GPUQueue.writeBuffer | data is not a BufferSource | a | [#dom-gpuqueue-writebuffer](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-writebuffer) (WebIDL: TypeError) | - |
| `shared/full-surface.js:68` | GPUQueue.writeBuffer | dataOffset/size exceed source | a | [#dom-gpuqueue-writebuffer](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-writebuffer) (content timeline: OperationError) | - |
| `shared/full-surface.js:137` | createComputePipeline/createRenderPipeline | entry point not in module (incl. invalid modules) | b | [#abstract-opdef-validating-gpuprogrammablestage](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpuprogrammablestage) | C: yes (bypass probe, captured by addon as above); Zig: yes |
| `shared/full-surface.js:537` | GPUDevice.pushErrorScope | unknown filter | a | [#dom-gpudevice-pusherrorscope](https://gpuweb.github.io/gpuweb/#dom-gpudevice-pusherrorscope) (enum: TypeError) | - |
| `shared/full-surface.js:547` | GPUAdapter.requestDevice | required feature unsupported | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) (reject TypeError) | - |
| `shared/full-surface.js:561` | GPUAdapter.requestDevice | unknown limit name | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) (reject OperationError) | - |
| `shared/full-surface.js:564` | GPUAdapter.requestDevice | limit not a safe integer | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) ([EnforceRange]: TypeError) | - |
| `shared/full-surface.js:567` | GPUAdapter.requestDevice | limit not an integer | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) ([EnforceRange] truncates; spec does not reject) | - |
| `shared/full-surface.js:572` | GPUAdapter.requestDevice | limit better than adapter | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) (reject OperationError) | - |
| `shared/full-surface.js:577` | GPUAdapter.requestDevice | alignment limit invalid/better than adapter | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) (reject OperationError) | - |
| `shared/full-surface.js:653` | layout "auto" | incompatible declarations across stages | b | [#abstract-opdef-default-pipeline-layout](https://gpuweb.github.io/gpuweb/#abstract-opdef-default-pipeline-layout) (pipeline creation validation) | not run |
| `shared/full-surface.js:669` | GPUDevice.createRenderPipeline | no fragment and no depthStencil | b | [#abstract-opdef-validating-gpurenderpipelinedescriptor](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpurenderpipelinedescriptor) | not run |
| `shared/full-surface.js:690` | GPUDevice.createRenderPipeline | no color target and no depthStencil | b | [#abstract-opdef-validating-gpurenderpipelinedescriptor](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpurenderpipelinedescriptor) | not run |
| `shared/full-surface.js:761` | GPUBuffer.mapAsync | offset+size > buffer size (thrown synchronously) | b | [#dom-gpubuffer-mapasync](https://gpuweb.github.io/gpuweb/#dom-gpubuffer-mapasync) (validation error + promise rejected with OperationError) | not run |
| `shared/full-surface.js:768` | GPUBuffer.mapAsync | other map validation failures (async rejection + scope capture) | b | [#dom-gpubuffer-mapasync](https://gpuweb.github.io/gpuweb/#dom-gpubuffer-mapasync) | JS already routes to the scope and rejects |
| `shared/full-surface.js:793` | GPUBuffer.mapAsync | mapping cancelled | a | [#dom-gpubuffer-mapasync](https://gpuweb.github.io/gpuweb/#dom-gpubuffer-mapasync) (reject AbortError) | - |
| `shared/full-surface.js:798` | GPUBuffer.mapAsync | rethrow | c | - | - |
| `shared/full-surface.js:807` | GPUBuffer.getMappedRange | range outside the mapping | a | [#dom-gpubuffer-getmappedrange](https://gpuweb.github.io/gpuweb/#dom-gpubuffer-getmappedrange) (OperationError) | - |
| `shared/full-surface.js:817` | GPUBuffer._readCopy (Doe) | range | c | - | - |
| `shared/full-surface.js:830` | GPUBuffer.assertMappedPrefixF32 (Doe) | argument check | c | - | - |
| `shared/full-surface.js:835` | GPUBuffer.assertMappedPrefixF32 (Doe) | mismatch | c | - | - |
| `shared/full-surface.js:844` | GPUBuffer.assertMappedPrefixF32 (Doe) | argument check | c | - | - |
| `shared/full-surface.js:852` | GPUBuffer.assertMappedPrefixF32 (Doe) | mismatch | c | - | - |
| `shared/full-surface.js:943` | GPUQueue.writeBuffer | bufferOffset+size > buffer.size | b | [#dom-gpuqueue-writebuffer](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-writebuffer) (device timeline validation error) | C: yes; Zig: yes (bypass probe) |
| `shared/full-surface.js:998` | GPUQueue.copyExternalImageToTexture | unsupported on this surface | c | - | - |
| `shared/full-surface.js:1004` | GPUQueue.copyExternalImageToTexture | source.source missing | a | required dictionary member (WebIDL: TypeError) | - |
| `shared/full-surface.js:1107` | GPUTexture.createView | format not texture format or viewFormats | b | [#dom-gputexture-createview](https://gpuweb.github.io/gpuweb/#dom-gputexture-createview) | C: yes; Zig: no (creates the view) (bypass probe) |
| `shared/full-surface.js:1110` | GPUTexture.createView | swizzle string malformed | b | texture-component-swizzle createView validation (unverified against final text) | not run |
| `shared/full-surface.js:1376` | GPUDevice.onuncapturederror | non-function handler | c | HTML EventHandler: no throw | - |
| `shared/full-surface.js:1392` | GPUDevice.createBuffer | descriptor RangeError (mappedAtCreation size) | a | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) (RangeError) | - |
| `shared/full-surface.js:1395` | GPUDevice.createBuffer | descriptor invalid and no scope/handler registered | b | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) | JS routes to the scope when one exists (shouldRouteDeviceError); C: yes; Zig: no for usage combos/0 |
| `shared/full-surface.js:1407` | GPUDevice.createBuffer | size > maxBufferSize and no scope/handler | b | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) | JS routes when a scope exists; C: yes |
| `shared/full-surface.js:1495` | GPUDevice.createComputePipeline | wrapped native/JS failure | b | [#dom-gpudevice-createcomputepipeline](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createcomputepipeline) | see index.js:2559 |
| `shared/full-surface.js:1530` | GPUDevice.createComputePipelineAsync | failure | a | [#dom-gpudevice-createcomputepipelineasync](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createcomputepipelineasync) (reject GPUPipelineError) | - |
| `shared/full-surface.js:1613` | GPUDevice.createTexture | descriptor/creation failure and no scope/handler | b | [#dom-gpudevice-createtexture](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createtexture) (format-feature cases are TypeError, see validation.js:616) | JS routes when a scope exists, but as "out-of-memory" for every failure |
| `shared/full-surface.js:1701` | GPUDevice.createRenderPipeline | wrapped failure | b | [#dom-gpudevice-createrenderpipeline](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createrenderpipeline) | see index.js:2729 |
| `shared/full-surface.js:1745` | GPUDevice.createRenderPipelineAsync | failure | a | [#dom-gpudevice-createrenderpipelineasync](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createrenderpipelineasync) (reject GPUPipelineError) | - |
| `shared/full-surface.js:1753` | GPUDevice.createRenderBundleEncoder | surface unavailable | c | - | - |
| `shared/full-surface.js:1766` | GPUDevice.createQuerySet | query sets unsupported on backend | c | [#dom-gpudevice-createqueryset](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createqueryset) (feature checks would be TypeError/validation) | - |
| `shared/full-surface.js:1787` | GPUDevice.importExternalTexture | unsupported on this surface | c | - | - |
| `shared/full-surface.js:1793` | GPUDevice.importExternalTexture | source missing | a | required member (TypeError) | - |
| `shared/full-surface.js:1803` | GPUDevice.importExternalTexture | label not a string | c | WebIDL USVString converts; spec does not throw | - |
| `shared/full-surface.js:1863` | GPUAdapter.requestDevice | adapter already consumed | a | [#dom-gpuadapter-requestdevice](https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice) (reject OperationError) | - |
| `shared/native-metal-canvas-backend.js:15` | GPUCanvasContext (Metal) | canvas dimension | c | - | - |
| `shared/native-metal-canvas-backend.js:25` | GPUCanvasContext (Metal) | not macOS | c | - | - |
| `shared/native-metal-canvas-backend.js:40` | GPUCanvasContext (Metal) | backend missing | c | - | - |
| `shared/native-metal-canvas-backend.js:94` | GPUCanvasContext.configure (Metal) | surface creation failed | c | - | - |
| `shared/native-metal-canvas-backend.js:101` | GPUCanvasContext.configure (Metal) | colorSpace other than srgb | c | [#dom-gpucanvascontext-configure](https://gpuweb.github.io/gpuweb/#dom-gpucanvascontext-configure) (display-p3 is valid) | - |
| `shared/native-metal-canvas-backend.js:107` | GPUCanvasContext.configure (Metal) | toneMapping other than standard | c | [#dom-gpucanvascontext-configure](https://gpuweb.github.io/gpuweb/#dom-gpucanvascontext-configure) | - |
| `shared/native-metal-canvas-backend.js:172` | GPUCanvasContext.getCurrentTexture (Metal) | no texture | c | - | - |
| `shared/resource-lifecycle.js:6` | failValidation helper | throws plain Error | - | - | - |
| `shared/resource-lifecycle.js:29` | assertObject (helper) | not an object | a | WebIDL dictionary conversion (TypeError) | - |
| `shared/resource-lifecycle.js:36` | assertArray (helper) | not an array | a | WebIDL sequence conversion (TypeError) | - |
| `shared/resource-lifecycle.js:43` | assertBoolean (helper) | not a boolean | c | WebIDL boolean converts any value; spec does not throw | - |
| `shared/resource-lifecycle.js:50` | assertString (helper) | not a string | c | WebIDL DOMString/USVString converts; spec does not throw | - |
| `shared/resource-lifecycle.js:57` | assertNonEmptyString (helper) | empty string | c | caller-dependent (empty entryPoint: validation error, b) | - |
| `shared/resource-lifecycle.js:66` | assertIntegerInRange (helper) | non-integer or outside [min,max] | a | [EnforceRange] (TypeError); callers with min >= 1 are listed separately as b | - |
| `shared/resource-lifecycle.js:80` | validatePositiveInteger (helper) | < 1 | c | caller-dependent | - |
| `shared/resource-lifecycle.js:89` | assertLiveResource (helper) | owning device destroyed | b | [#dom-gpudevice-destroy](https://gpuweb.github.io/gpuweb/#dom-gpudevice-destroy) / lost device: calls become no-ops, no error | - |
| `shared/resource-lifecycle.js:96` | assertLiveResource (helper) | not a Doe WebGPU object | a | WebIDL interface conversion (TypeError) | - |
| `shared/resource-lifecycle.js:98` | assertLiveResource (helper) | object destroyed | b | object-specific: destroyed buffer/texture use is a validation error (e.g. #dom-gpuqueue-writebuffer, #dom-gputexture-createview); destroyed device: no-op | C: yes for destroyed buffer in writeBuffer/submit |
| `shared/resource-lifecycle.js:114` | GPUOrigin3D | sequence longer than 3 | a | [#abstract-opdef-validate-gpuorigin3d-shape](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-gpuorigin3d-shape) (TypeError) | - |
| `shared/resource-lifecycle.js:125` | GPUExtent3D | sequence length not 1..3 | a | [#abstract-opdef-validate-gpuextent3d-shape](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-gpuextent3d-shape) (TypeError) | - |
| `shared/resource-lifecycle.js:129` | GPUExtent3D | width missing | a | required member (TypeError) | - |
| `shared/validation.js:30` | GPUColor | sequence length not 4 | a | [#abstract-opdef-validate-gpucolor-shape](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-gpucolor-shape) (TypeError) | - |
| `shared/validation.js:34` | GPUColor | non-finite component | a | GPUColor double conversion (TypeError) | - |
| `shared/validation.js:495` | GPUDevice.createBuffer | unknown usage bits | b | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) | routed when a scope exists (full-surface.js:1395) |
| `shared/validation.js:501` | GPUDevice.createBuffer | MAP_READ with other than COPY_DST | b | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) | C: yes; Zig: no (bypass probe); routed when a scope exists |
| `shared/validation.js:507` | GPUDevice.createBuffer | MAP_WRITE with other than COPY_SRC | b | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) | routed when a scope exists |
| `shared/validation.js:512` | GPUDevice.createBuffer | mappedAtCreation size not multiple of 4 | a | [#dom-gpudevice-createbuffer](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer) (RangeError) | - |
| `shared/validation.js:529` | enum fields (helper) | unknown enum value | a | WebIDL enum (TypeError) | - |
| `shared/validation.js:537` | GPUFeatureName | unknown feature | a | WebIDL enum (TypeError) | - |
| `shared/validation.js:577` | numeric fields (helper) | non-finite | a | WebIDL float/double (TypeError) | - |
| `shared/validation.js:613` | GPUTextureFormat | unknown format | a | WebIDL enum (TypeError) | - |
| `shared/validation.js:616` | GPUTextureFormat | BC format without feature | a | [#abstract-opdef-validating-texture-format-required-features](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-texture-format-required-features) (TypeError) | - |
| `shared/validation.js:619` | GPUTextureFormat | ETC2 format without feature | a | [#abstract-opdef-validating-texture-format-required-features](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-texture-format-required-features) (TypeError) | - |
| `shared/validation.js:622` | GPUTextureFormat | ASTC format without feature | a | [#abstract-opdef-validating-texture-format-required-features](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-texture-format-required-features) (TypeError) | - |
| `shared/validation.js:625` | GPUTextureFormat | depth32float-stencil8 without feature | a | [#abstract-opdef-validating-texture-format-required-features](https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-texture-format-required-features) (TypeError) | - |
| `shared/validation.js:634` | GPUSamplerBindingLayout | unknown type | a | WebIDL enum (TypeError) | - |
| `shared/validation.js:653` | GPUTextureBindingLayout | unknown view dimension | a | WebIDL enum (TypeError) | - |
| `shared/validation.js:666` | GPUDevice.createBindGroupLayout | storage format/access combination unsupported | b | [#dom-gpudevice-createbindgrouplayout](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgrouplayout) | not run |
| `shared/validation.js:709` | GPUAdapter.requestDevice | defaultQueue.label not a string | c | WebIDL USVString converts; spec does not throw | - |
| `shared/validation.js:724` | GPUAdapter.requestDevice | descriptor normalization failed | a | WebIDL dictionary conversion (TypeError) | - |
| `shared/validation.js:776` | GPUDevice.createSampler | lodMinClamp > lodMaxClamp | b | [#dom-gpudevice-createsampler](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createsampler) | C: yes; Zig: no (creates the sampler) (bypass probe) |
| `shared/validation.js:796` | GPUTexture.createView | swizzle without texture-component-swizzle | b | createView validation (feature-gated member; unverified against final text) | not run |
| `shared/validation.js:994` | GPUExtent3D (texture size) | array length not 1..3 | a | [#abstract-opdef-validate-gpuextent3d-shape](https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-gpuextent3d-shape) (TypeError) | - |
| `shared/validation.js:1017` | GPUBindGroupEntry.resource | not an object | a | WebIDL union conversion (TypeError) | - |
| `shared/validation.js:1053` | GPUBindGroupEntry.resource | not a resource type | a | WebIDL union conversion (TypeError) | - |
| `validation.js:493 / full-surface.js:1602` | createBuffer / createTexture | usage 0 (assertIntegerInRange min 1) | b | [#dom-gpudevice-createbuffer,](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbuffer,) #dom-gpudevice-createtexture | createBuffer: C yes, Zig no (bypass probe) |
| `validation.js:987-1005` | GPUDevice.createTexture | size width/height/depth 0 (min 1) | b | [#dom-gpudevice-createtexture](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createtexture) | not run |
| `index.js:2667-2668` | GPUDevice.createTexture | mipLevelCount or sampleCount 0 | b | [#dom-gpudevice-createtexture](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createtexture) | not run |
| `full-surface.js:1100,1104` | GPUTexture.createView | mipLevelCount or arrayLayerCount 0 | b | [#dom-gputexture-createview](https://gpuweb.github.io/gpuweb/#dom-gputexture-createview) | not run |
| `full-surface.js:314 / validation.js:1025` | GPUDevice.createBindGroup | buffer binding size 0 | b | [#dom-gpudevice-createbindgroup](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgroup) | not run |
| `full-surface.js:1054-1055` | GPUQueue.copyExternalImageToTexture | copySize width/height 0 (empty copies are valid) | b | [#dom-gpuqueue-copyexternalimagetotexture](https://gpuweb.github.io/gpuweb/#dom-gpuqueue-copyexternalimagetotexture) (0 is allowed: Doe rejects a valid call) | - |
| `full-surface.js:1763` | GPUDevice.createQuerySet | count 0 | c | [#dom-gpudevice-createqueryset](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createqueryset) (only count > 4096 is an error) | - |

## Package tests that assert a throw for a class-b case

These would need to change (expect a scope/uncapturederror error, or an invalid object) if class b stops throwing:

- `test/integration/test-integration-gpu-namespace.js:583-593`: `encoder.finish()` with an open compute pass must throw "active pass" (spec: encoder invalidated, validation error at finish).
- `test/unit/full-surface-lifecycle.test.js:62`: `device.queue.submit([])` after `device.destroy()` must throw /destroyed/ (spec: lost device, no-op without error).
- `test/integration/test-integration-native-buffer-map.js:82`: same post-destroy submit throw.
- `test/integration/test-integration-native-command-ownership.js:39-44`: finishing an invalid encoder and resubmitting or duplicating command buffers throw. This drives the native-direct surface (`createNativeDirect`, Doe-only), so it changes only if that surface follows the same decision.

No package test asserts a throw for the writeBuffer range, invalid-module pipeline, sampler, buffer-usage or createView cases above; the 2026-10-23 `test-integration-render-state-values.js` only asserts scope delivery for createShaderModule and writeTexture.
