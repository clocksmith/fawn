// Shared Node/Bun submission and readback diagnostic breakdown definitions.

const NS_PER_MS = 1_000_000;
const DISPATCH_FLUSH_BREAKDOWN_FIELD_COUNT = 12;
const DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY = 0;
const DISPATCH_FLUSH_BREAKDOWN_QUEUE_SUBMIT = 1;
const DISPATCH_FLUSH_BREAKDOWN_FLUSH = 2;
const DISPATCH_FLUSH_BREAKDOWN_WAIT_COMPLETED = 3;
const DISPATCH_FLUSH_BREAKDOWN_DEFERRED_COPY = 4;
const DISPATCH_FLUSH_BREAKDOWN_DEFERRED_RESOLVE = 5;
const DISPATCH_FLUSH_BREAKDOWN_COMMAND_BUFFER_END = 6;
const DISPATCH_FLUSH_BREAKDOWN_SYNC_PREPARE = 7;
const DISPATCH_FLUSH_BREAKDOWN_DRIVER_SUBMIT = 8;
const DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_PREPARE = 9;
const DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_RECORD = 10;
const DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_COPY = 11;

const QUEUE_SUBMIT_BREAKDOWN_FIELDS = Object.freeze([
  'submitCommandPrepTotalNs',
  'submitAddonCallTotalNs',
  'submitAddonCommandReplayTotalNs',
  'submitAddonCommandReplayPrepareTotalNs',
  'submitAddonCommandReplayRecordTotalNs',
  'submitAddonCommandReplayCopyTotalNs',
  'submitAddonQueueSubmitTotalNs',
  'submitAddonCommandBufferEndTotalNs',
  'submitAddonSyncPrepareTotalNs',
  'submitAddonDriverSubmitTotalNs',
  'submitAddonFlushTotalNs',
  'submitPostSubmitBookkeepingTotalNs',
  'submitQueueFlushTotalNs',
  'submitQueueFlushWaitCompletedTotalNs',
  'submitQueueFlushDeferredCopyTotalNs',
  'submitQueueFlushDeferredResolveTotalNs',
  'submitQueueWaitBookkeepingTotalNs',
]);

const ADDON_SUBMIT_BREAKDOWN_FIELD_ENTRIES = Object.freeze([
  Object.freeze(['submitAddonCommandReplayTotalNs', 'commandReplayNs', DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY]),
  Object.freeze(['submitAddonCommandReplayPrepareTotalNs', 'commandReplayPrepareNs', DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_PREPARE]),
  Object.freeze(['submitAddonCommandReplayRecordTotalNs', 'commandReplayRecordNs', DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_RECORD]),
  Object.freeze(['submitAddonCommandReplayCopyTotalNs', 'commandReplayCopyNs', DISPATCH_FLUSH_BREAKDOWN_COMMAND_REPLAY_COPY]),
  Object.freeze(['submitAddonQueueSubmitTotalNs', 'queueSubmitNs', DISPATCH_FLUSH_BREAKDOWN_QUEUE_SUBMIT]),
  Object.freeze(['submitAddonCommandBufferEndTotalNs', 'commandBufferEndNs', DISPATCH_FLUSH_BREAKDOWN_COMMAND_BUFFER_END]),
  Object.freeze(['submitAddonSyncPrepareTotalNs', 'syncPrepareNs', DISPATCH_FLUSH_BREAKDOWN_SYNC_PREPARE]),
  Object.freeze(['submitAddonDriverSubmitTotalNs', 'driverSubmitNs', DISPATCH_FLUSH_BREAKDOWN_DRIVER_SUBMIT]),
  Object.freeze(['submitAddonFlushTotalNs', 'flushNs', DISPATCH_FLUSH_BREAKDOWN_FLUSH]),
  Object.freeze(['submitQueueFlushWaitCompletedTotalNs', 'waitCompletedNs', DISPATCH_FLUSH_BREAKDOWN_WAIT_COMPLETED]),
  Object.freeze(['submitQueueFlushDeferredCopyTotalNs', 'deferredCopyNs', DISPATCH_FLUSH_BREAKDOWN_DEFERRED_COPY]),
  Object.freeze(['submitQueueFlushDeferredResolveTotalNs', 'deferredResolveNs', DISPATCH_FLUSH_BREAKDOWN_DEFERRED_RESOLVE]),
]);

const ZERO_READBACK_BREAKDOWN_NS = Object.freeze({
  readbackMapReadCopyUnmapQueueWaitCompletedTotalNs: 0,
  readbackMapReadCopyUnmapDeferredCopyTotalNs: 0,
  readbackMapReadCopyUnmapDeferredResolveTotalNs: 0,
  readbackMapReadCopyUnmapMapTotalNs: 0,
  readbackMapReadCopyUnmapCopyTotalNs: 0,
  readbackMapReadCopyUnmapUnmapTotalNs: 0,
});

function zeroQueueSubmitBreakdown() {
  return {
    submitCommandPrepTotalNs: 0,
    submitAddonCallTotalNs: 0,
    submitAddonCommandReplayTotalNs: 0,
    submitAddonCommandReplayPrepareTotalNs: 0,
    submitAddonCommandReplayRecordTotalNs: 0,
    submitAddonCommandReplayCopyTotalNs: 0,
    submitAddonQueueSubmitTotalNs: 0,
    submitAddonCommandBufferEndTotalNs: 0,
    submitAddonSyncPrepareTotalNs: 0,
    submitAddonDriverSubmitTotalNs: 0,
    submitAddonFlushTotalNs: 0,
    submitPostSubmitBookkeepingTotalNs: 0,
    submitQueueFlushTotalNs: 0,
    submitQueueFlushWaitCompletedTotalNs: 0,
    submitQueueFlushDeferredCopyTotalNs: 0,
    submitQueueFlushDeferredResolveTotalNs: 0,
    submitQueueWaitBookkeepingTotalNs: 0,
  };
}

function elapsedNsSince(startedAtMs) {
  return Math.max(0, Math.round((performance.now() - startedAtMs) * NS_PER_MS));
}

export {
  ADDON_SUBMIT_BREAKDOWN_FIELD_ENTRIES,
  DISPATCH_FLUSH_BREAKDOWN_FIELD_COUNT,
  NS_PER_MS,
  QUEUE_SUBMIT_BREAKDOWN_FIELDS,
  ZERO_READBACK_BREAKDOWN_NS,
  elapsedNsSince,
  zeroQueueSubmitBreakdown,
};
