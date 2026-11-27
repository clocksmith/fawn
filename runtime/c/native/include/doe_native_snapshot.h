/* SPDX-License-Identifier: MIT
 * Doe GPU-state snapshot extension (not webgpu.h): tracked restore, stage 1.
 * Listed in ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress.
 *
 * A snapshot freezes a set of writable buffers and 2D textures as the base
 * state. Between BranchBegin and BranchEnd every queue submission on the device
 * runs against one private slot copy of that set: bindings, copies, clears and
 * render attachments that name a tracked resource are lowered against the
 * slot, so the base resources are never written. The compiler marks the 64 KiB
 * page of every storage-buffer store and atomic and the 256x256 tile of every
 * 2D textureStore; submission marks the destination ranges of copies, clears,
 * query resolves and render attachments. BranchBegin fills a new slot with a
 * full copy and refills a recycled slot by copying back only the pages and
 * tiles the previous branch marked.
 *
 * Stage 1 limits, each refused with a device error instead of run unmarked:
 * Vulkan devices only; one snapshot and one slot per device; write marks must
 * be enabled before any shader module is created; tracked buffers need
 * CopySrc, no map usage and must be unmapped; tracked textures are
 * single-sample, single-mip, single-layer 2D with CopySrc; a branch submission
 * that dispatches a pipeline compiled without marks, and queue writes to a
 * tracked resource, are refused. Outside a branch the base stays frozen while
 * the snapshot lives: submissions and compute programs that would write a
 * tracked resource (copies, clears, attachments, writable storage bindings)
 * are refused, since the next restore copies back only the previous branch's
 * units. Restore reads the marks on the host after the caller has observed
 * completion of the branch's work. */
#ifndef DOE_NATIVE_SNAPSHOT_H
#define DOE_NATIVE_SNAPSHOT_H
#include "webgpu.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

typedef struct DoeNativeSnapshotDesc {
    size_t bufferCount;
    const WGPUBuffer *buffers;
    size_t textureCount;
    const WGPUTexture *textures;
} DoeNativeSnapshotDesc;

/* Byte and event counters; every byte is counted from the commands recorded. */
typedef struct DoeNativeSnapshotLedger {
    uint64_t declaredBytes;      /* Bytes of the tracked set: a D3 copy per branch. */
    uint64_t branchesBegun;
    uint64_t branchesEnded;
    uint64_t lastPrepareBytes;   /* Bytes copied into the slot for the last begun branch. */
    uint64_t totalPrepareBytes;
    uint64_t lastPrepareCopies;  /* Copy commands recorded for the last begun branch. */
    uint64_t lastMarkedUnits;    /* Pages plus tiles the last ended branch marked. */
    uint64_t lastMarkedBytes;    /* Their bytes, clipped to each resource. */
    uint64_t prepareSubmits;
    uint64_t branchSubmits;
    uint64_t slotViews;          /* Image views over slot textures the snapshot holds now. */
} DoeNativeSnapshotLedger;

/* 1 when every later WGSL module on this Vulkan device compiles with write
 * marks; 0 otherwise. Must precede the first shader module of the device. */
DOE_NATIVE_EXTENSION uint32_t doeNativeSnapshotEnableWriteMarks(WGPUDevice device);
/* Freezes the given resources as the base. NULL reports a device error. */
DOE_NATIVE_EXTENSION void *doeNativeSnapshotCreate(WGPUDevice device, const DoeNativeSnapshotDesc *desc);
/* Enqueues the slot fill or marked-page restore, then routes later
 * submissions to the slot. 1 on success. */
DOE_NATIVE_EXTENSION uint32_t doeNativeSnapshotBranchBegin(void *snapshot);
/* Ends the branch after the caller observed completion of its submissions;
 * reads the branch's marks for the next restore. 1 on success. */
DOE_NATIVE_EXTENSION uint32_t doeNativeSnapshotBranchEnd(void *snapshot);
DOE_NATIVE_EXTENSION uint32_t doeNativeSnapshotGetLedger(void *snapshot, DoeNativeSnapshotLedger *out);
/* Marks of the last ended branch for one tracked resource (buffers first, then
 * textures): one byte per page or tile, 1 when marked. Returns the unit count;
 * writes at most capacity bytes. 0 for an invalid index. */
DOE_NATIVE_EXTENSION size_t doeNativeSnapshotGetMarks(void *snapshot, size_t resource,
    uint8_t *out, size_t capacity);
/* Waits for no work: the caller must have observed completion of every
 * submission that used the slot. */
DOE_NATIVE_EXTENSION void doeNativeSnapshotRelease(void *snapshot);
#ifdef __cplusplus
}
#endif
#endif
