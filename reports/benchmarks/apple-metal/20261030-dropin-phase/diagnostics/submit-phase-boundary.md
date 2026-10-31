# Real copy-submit path, read-only trace

Consumer: the remaining Metal copy-submit gap. This source read corrects the
interpretation of the older native probe; it is not a timing verdict.
Doe source: `125654e63`; Dawn source: `d04f88359`.

Dawn's public queue write (`src/dawn/native/Queue.cpp:338`) stages the upload
through `Buffer.cpp:1136` and `metal/DeviceMTL.mm:327`. The pending command
context's `EnsureBlit` (`metal/CommandRecordingContext.mm:103`) leaves its blit
encoder open. The copy in timed submit reuses it
(`metal/CommandBufferMTL.mm:1220-1240`); `AcquireCommands` closes it
(`CommandRecordingContext.mm:78-87`). These workloads remain below the uploader's
16 MiB submission threshold (`DynamicUploader.cpp:142-155`). The next command
buffer is prepared after commit (`metal/QueueMTL.mm:253`), inside submit.

Doe writes its Shared allocation on the CPU (`runtime/c/native/src/buffers.c:438`,
`runtime/c/backends/metal/src/resources.c:79`). Its timed submit opens a new blit
encoder (`objc_commands.c:23-34`). Dawn's nonmappable input is Private
(`metal/BufferMTL.mm:69-75`); Doe's buffers are Shared
(`runtime/c/native/src/backend_buffers.c:28-30`). Both paths preserve actual
completion and full-output checks. Complete-operation timings include the write
and submit on both providers.

The retained `submit_probe.m:35-44` creates the supposed Dawn blit inside its
interval and uses a Shared input. Its `:53-59` pool variant uses
`@autoreleasepool`; actual Doe uses `NSAutoreleasePool new/drain`
(`objc_runtime.c:19-24`). Thus equal probe timings do not establish equivalent
work inside the real submit interval, and the probe's pool variant does not
measure the actual Doe pool adapter.

Dawn's actual dylib imports `objc_autoreleasePoolPush/Pop`; the installed SDK's
`usr/lib/libobjc.A.tbd:70-71` exports both. A separate candidate may test those
primitives while preserving each existing scope. Foundation can already use them
internally: no speedup is inferred. The decisive next diagnostic separates actual
pool entry, encoder creation, copy, endEncoding and pool drain, using Dawn's
already-open upload encoder as its control. No source changes or GPU run were
needed for this trace.
