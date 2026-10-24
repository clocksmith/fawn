import {
  UINT32_MAX,
  failValidation,
  describeResourceLabel,
  initResource,
  markInvalid,
  isInvalidResource,
  assertObject,
  assertArray,
  assertIntegerInRange,
  assertLiveResource,
  destroyResource,
  normalizeOrigin3D,
  normalizeExtent3D,
} from './resource-lifecycle.js';
import { normalizeGpuColor } from './validation.js';
import {
  shouldRouteDeviceError,
  isDeviceLost,
  deliverDeviceError,
  collectDeviceErrors,
  GPUValidationError,
} from './full-surface.js';

const UINT32_RANGE = Object.freeze({ min: 0, max: UINT32_MAX });
const NON_NEGATIVE_RANGE = Object.freeze({ min: 0 });
const POSITIVE_RANGE = Object.freeze({ min: 1 });
const POSITIVE_UINT32_RANGE = Object.freeze({ min: 1, max: UINT32_MAX });

// https://gpuweb.github.io/gpuweb/#dom-gpubindingcommandsmixin-setimmediates
// (content timeline): dataOffset and size count elements of a typed array
// (bytes for an ArrayBuffer); a range past the data or a byte size that is not a
// multiple of 4 throws OperationError. rangeOffset limits are device validation.
function immediateOperationError(path, message) {
  const text = `${path}: ${message}`;
  if (typeof DOMException === 'function') return new DOMException(text, 'OperationError');
  const error = new Error(text);
  error.name = 'OperationError';
  return error;
}

function normalizeImmediateDataInput(data, dataOffset = 0, size, path) {
  const isSharedArrayBuffer = typeof SharedArrayBuffer !== 'undefined' && data instanceof SharedArrayBuffer;
  if (!ArrayBuffer.isView(data) && !(data instanceof ArrayBuffer) && !isSharedArrayBuffer) {
    failValidation(path, 'data must be an ArrayBuffer, SharedArrayBuffer, TypedArray, or DataView');
  }
  assertIntegerInRange(dataOffset, path, 'dataOffset', { min: 0 });
  if (size !== undefined) {
    assertIntegerInRange(size, path, 'size', { min: 0 });
  }
  const elementSize = ArrayBuffer.isView(data) && !(data instanceof DataView) ? data.BYTES_PER_ELEMENT : 1;
  const bytes = ArrayBuffer.isView(data)
    ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
    : new Uint8Array(data);
  const elements = bytes.byteLength / elementSize;
  const count = size === undefined ? Math.max(0, elements - dataOffset) : size;
  if (dataOffset + count > elements) {
    throw immediateOperationError(path, `data range ${dataOffset}+${count} exceeds ${elements} elements`);
  }
  if ((count * elementSize) % 4 !== 0) {
    throw immediateOperationError(path, `content size ${count * elementSize} bytes is not a multiple of 4`);
  }
  return bytes.subarray(dataOffset * elementSize, (dataOffset + count) * elementSize);
}

// WebIDL [EnforceRange] integer conversion: ToNumber, reject NaN and
// infinities, truncate, then range-check (TypeError on failure).
function enforceRange(value, path, label, max) {
  if (typeof value === 'bigint' || typeof value === 'symbol') {
    throw new TypeError(`${path}: ${label} must be a number`);
  }
  const number = Number(value);
  if (!Number.isFinite(number)) {
    throw new TypeError(`${path}: ${label} must be a finite number`);
  }
  const integer = Math.trunc(number);
  if (integer < 0 || integer > max) {
    throw new TypeError(`${path}: ${label} ${integer} is outside [0, ${max}]`);
  }
  return integer;
}

// setBindGroup(index, bindGroup, dynamicOffsets = []) and
// setBindGroup(index, bindGroup, dynamicOffsetsData, dataStart, dataLength).
// Returns the offsets as a Uint32Array copy, or null when there are none.
function normalizeDynamicOffsets(path, argumentCount, offsets, dataStart, dataLength) {
  if (argumentCount > 3) {
    if (!(offsets instanceof Uint32Array)) {
      throw new TypeError(`${path}: dynamicOffsetsData must be a Uint32Array`);
    }
    const start = enforceRange(dataStart, path, 'dynamicOffsetsDataStart', Number.MAX_SAFE_INTEGER);
    const length = enforceRange(dataLength, path, 'dynamicOffsetsDataLength', UINT32_MAX);
    if (start + length > offsets.length) {
      throw new RangeError(`${path}: dynamicOffsetsDataStart + dynamicOffsetsDataLength exceeds dynamicOffsetsData.length`);
    }
    return length === 0 ? null : offsets.slice(start, start + length);
  }
  if (offsets === undefined) {
    return null;
  }
  if (offsets === null || typeof offsets[Symbol.iterator] !== 'function') {
    throw new TypeError(`${path}: dynamicOffsets must be a sequence of GPUBufferDynamicOffset`);
  }
  const values = Array.from(offsets, (value) => enforceRange(value, path, 'dynamic offset', UINT32_MAX));
  return values.length === 0 ? null : Uint32Array.from(values);
}

// GPUBindGroup? : null and undefined both unbind the slot.
function normalizeBindGroup(bindGroup, path) {
  return bindGroup == null ? null : assertLiveResource(bindGroup, path, 'GPUBindGroup');
}

// === Invalid objects in encoders ===
// A command that names an invalid object makes its encoder invalid (WebGPU
// "invalidate the encoder"): it records nothing, later binding, draw and
// dispatch commands record nothing, and finish() reports the error. With no
// error scope or uncapturederror listener the use throws at once instead.
// finish() generates one validation error at most and never throws: the
// first validation error reported while it ran (package or provider), else
// the invalid-object use that invalidated the encoder. An out-of-memory or
// internal error is a different error and still reaches its own scope (first
// of each). Unobserved errors are dropped (WebGPU).
function reportFinishError(device, errors, invalidMessage) {
  const reported = new Set();
  for (const { filter, error } of errors) {
    if (reported.has(filter)) continue;
    reported.add(filter);
    deliverDeviceError(device, filter, error);
  }
  if (invalidMessage != null && !reported.has('validation')) {
    deliverDeviceError(device, 'validation', new GPUValidationError(invalidMessage));
  }
}

function invalidUseMessage(path, first, second) {
  const invalid = isInvalidResource(first) ? first : (isInvalidResource(second) ? second : null);
  return invalid === null
    ? null
    : `${path}: ${describeResourceLabel(invalid)} is invalid because its creation failed validation`;
}

function invalidateEncoder(encoder, message) {
  if (!shouldRouteDeviceError(encoder._device)) {
    throw new Error(message);
  }
  if (encoder._invalidMessage == null) {
    encoder._invalidMessage = message;
  }
}

// True when a command that names `first`/`second` must record nothing: the
// device is lost (every command is inert), or an object is invalid.
function encoderSkipsUse(encoder, path, first = null, second = null) {
  if (isDeviceLost(encoder._device)) {
    return true;
  }
  const message = invalidUseMessage(path, first, second);
  if (message !== null) {
    invalidateEncoder(encoder, message);
    return true;
  }
  return false;
}

// True when a binding, draw or dispatch command must record nothing: its pass
// has no native pass, its encoder is already invalid, or it names an invalid
// object.
function passSkipsCommand(pass, encoder, path, first = null, second = null) {
  if (pass === encoder && encoder._finished === true && !isDeviceLost(encoder._device)) {
    // A render bundle encoder after finish(): a validation error, and nothing
    // reaches its finished native encoder.
    deliverDeviceError(encoder._device, 'validation', new GPUValidationError(`${path}: the render bundle encoder is already finished`));
    return true;
  }
  return pass._invalid || encoder._invalidMessage != null || encoderSkipsUse(encoder, path, first, second);
}

// A pass or bundle encoder that records nothing: it has no native pass, or
// its device is lost.
function passInert(pass) {
  return pass._invalid || isDeviceLost(pass._device ?? pass._encoder?._device);
}

// Objects a render pass descriptor names, for the invalid-object check.
function renderPassDescriptorObjects(descriptor) {
  const objects = [];
  if (Array.isArray(descriptor.colorAttachments)) {
    for (const attachment of descriptor.colorAttachments) {
      objects.push(attachment?.view, attachment?.resolveTarget);
    }
  }
  objects.push(
    descriptor.depthStencilAttachment?.view,
    descriptor.occlusionQuerySet,
    descriptor.timestampWrites?.querySet,
  );
  return objects;
}

function normalizeCopySize(copySize, path) {
  return normalizeExtent3D(copySize, path, 'copySize');
}

// Omitted strides stay undefined (WGPU_COPY_STRIDE_UNDEFINED at the addon); an
// explicit 0 is a real stride the spec validates against the copy height.
function assertOptionalStride(value, path, label) {
  if (value !== undefined) assertIntegerInRange(value, path, label, UINT32_RANGE);
  return value;
}

function createEncoderClasses(backend) {
  let classes = null;

  const _hasComputePassAssertOpen = typeof backend.computePassAssertOpen === 'function';
  const commandBufferFinalizer = typeof FinalizationRegistry === 'function' && typeof backend.commandBufferDestroy === 'function'
    ? new FinalizationRegistry((native) => {
      backend.commandBufferDestroy(native);
    })
    : null;

  function releaseCommandBuffer(commandBuffer) {
    if (commandBuffer._destroyed) {
      return;
    }
    if (commandBuffer._finalizerToken && commandBufferFinalizer) {
      commandBufferFinalizer.unregister(commandBuffer._finalizerToken);
      commandBuffer._finalizerToken = null;
    }
    commandBuffer._commands = [];
    const native = commandBuffer._native;
    commandBuffer._native = null;
    commandBuffer._destroyed = true;
    if (native != null && typeof backend.commandBufferDestroy === 'function') {
      backend.commandBufferDestroy(native);
    }
  }

  class DoeGPUComputePassEncoder {
    constructor(state, encoder) {
      this._encoder = encoder;
      this.label = '';
      initResource(this, 'GPUComputePassEncoder', encoder);
      backend.computePassInit(this, state);
    }

    // On a lost device every call is inert, so state errors do not throw.
    _assertOpen(path) {
      if (isDeviceLost(this._encoder?._device)) return;
      if (_hasComputePassAssertOpen) {
        backend.computePassAssertOpen(this, path);
      }
    }

    setPipeline(pipeline) {
      this._assertOpen('GPUComputePassEncoder.setPipeline');
      if (passSkipsCommand(this, this._encoder, 'GPUComputePassEncoder.setPipeline', pipeline)) return;
      backend.computePassSetPipeline(
        this,
        assertLiveResource(pipeline, 'GPUComputePassEncoder.setPipeline', 'GPUComputePipeline'),
      );
    }

    setBindGroup(index, bindGroup, dynamicOffsets, dynamicOffsetsDataStart, dynamicOffsetsDataLength) {
      const path = 'GPUComputePassEncoder.setBindGroup';
      this._assertOpen(path);
      assertIntegerInRange(index, path, 'index', UINT32_RANGE);
      const offsets = normalizeDynamicOffsets(
        path,
        arguments.length,
        dynamicOffsets,
        dynamicOffsetsDataStart,
        dynamicOffsetsDataLength,
      );
      if (passSkipsCommand(this, this._encoder, path, bindGroup)) return;
      backend.computePassSetBindGroup(this, index, normalizeBindGroup(bindGroup, path), offsets);
    }

    setImmediates(index, data, dataOffset = 0, size) {
      this._assertOpen('GPUComputePassEncoder.setImmediates');
      assertIntegerInRange(index, 'GPUComputePassEncoder.setImmediates', 'index', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPUComputePassEncoder.setImmediates')) return;
      backend.computePassSetImmediates(
        this,
        index,
        normalizeImmediateDataInput(data, dataOffset, size, 'GPUComputePassEncoder.setImmediates'),
      );
    }

    dispatchWorkgroups(x, y = 1, z = 1) {
      this._assertOpen('GPUComputePassEncoder.dispatchWorkgroups');
      assertIntegerInRange(x, 'GPUComputePassEncoder.dispatchWorkgroups', 'x', UINT32_RANGE);
      assertIntegerInRange(y, 'GPUComputePassEncoder.dispatchWorkgroups', 'y', UINT32_RANGE);
      assertIntegerInRange(z, 'GPUComputePassEncoder.dispatchWorkgroups', 'z', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPUComputePassEncoder.dispatchWorkgroups')) return;
      backend.computePassDispatchWorkgroups(this, x, y, z);
    }

    _dispatchBound(pipeline, bindGroup, x, y = 1, z = 1) {
      this._assertOpen('GPUComputePassEncoder._dispatchBound');
      assertIntegerInRange(x, 'GPUComputePassEncoder._dispatchBound', 'x', UINT32_RANGE);
      assertIntegerInRange(y, 'GPUComputePassEncoder._dispatchBound', 'y', UINT32_RANGE);
      assertIntegerInRange(z, 'GPUComputePassEncoder._dispatchBound', 'z', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPUComputePassEncoder._dispatchBound', pipeline, bindGroup)) return;
      backend.computePassDispatchBound(
        this,
        assertLiveResource(pipeline, 'GPUComputePassEncoder._dispatchBound', 'GPUComputePipeline'),
        bindGroup == null
          ? null
          : assertLiveResource(bindGroup, 'GPUComputePassEncoder._dispatchBound', 'GPUBindGroup'),
        x,
        y,
        z,
      );
    }

    dispatchWorkgroupsIndirect(indirectBuffer, indirectOffset = 0) {
      this._assertOpen('GPUComputePassEncoder.dispatchWorkgroupsIndirect');
      assertIntegerInRange(indirectOffset, 'GPUComputePassEncoder.dispatchWorkgroupsIndirect', 'indirectOffset', NON_NEGATIVE_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPUComputePassEncoder.dispatchWorkgroupsIndirect', indirectBuffer)) return;
      backend.computePassDispatchWorkgroupsIndirect(
        this,
        assertLiveResource(indirectBuffer, 'GPUComputePassEncoder.dispatchWorkgroupsIndirect', 'GPUBuffer'),
        indirectOffset,
      );
    }

    pushDebugGroup(groupLabel) {
      this._assertOpen('GPUComputePassEncoder.pushDebugGroup');
      if (passInert(this)) return;
      backend.computePassPushDebugGroup(this, groupLabel);
    }
    popDebugGroup() {
      this._assertOpen('GPUComputePassEncoder.popDebugGroup');
      if (passInert(this)) return;
      backend.computePassPopDebugGroup(this);
    }
    insertDebugMarker(markerLabel) {
      this._assertOpen('GPUComputePassEncoder.insertDebugMarker');
      if (passInert(this)) return;
      backend.computePassInsertDebugMarker(this, markerLabel);
    }

    end() {
      this._assertOpen('GPUComputePassEncoder.end');
      if (passInert(this)) {
        this._ended = true;
        return;
      }
      backend.computePassEnd(this);
      if (this._encoder._openPass === this) this._encoder._openPass = null;
    }
  }

  class DoeGPURenderPassEncoder {
    constructor(state, encoder) {
      this._encoder = encoder;
      this.label = '';
      initResource(this, 'GPURenderPassEncoder', encoder);
      backend.renderPassInit(this, state);
    }

    _assertOpen(path) {
      if (isDeviceLost(this._encoder?._device)) return;
      if (typeof backend.renderPassAssertOpen === 'function') {
        backend.renderPassAssertOpen(this, path);
      }
    }

    setPipeline(pipeline) {
      this._assertOpen('GPURenderPassEncoder.setPipeline');
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.setPipeline', pipeline)) return;
      backend.renderPassSetPipeline(
        this,
        assertLiveResource(pipeline, 'GPURenderPassEncoder.setPipeline', 'GPURenderPipeline'),
      );
    }

    setBindGroup(index, bindGroup, dynamicOffsets, dynamicOffsetsDataStart, dynamicOffsetsDataLength) {
      const path = 'GPURenderPassEncoder.setBindGroup';
      this._assertOpen(path);
      assertIntegerInRange(index, path, 'index', UINT32_RANGE);
      const offsets = normalizeDynamicOffsets(
        path,
        arguments.length,
        dynamicOffsets,
        dynamicOffsetsDataStart,
        dynamicOffsetsDataLength,
      );
      if (passSkipsCommand(this, this._encoder, path, bindGroup)) return;
      backend.renderPassSetBindGroup(this, index, normalizeBindGroup(bindGroup, path), offsets);
    }

    setImmediates(index, data, dataOffset = 0, size) {
      this._assertOpen('GPURenderPassEncoder.setImmediates');
      assertIntegerInRange(index, 'GPURenderPassEncoder.setImmediates', 'index', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.setImmediates')) return;
      backend.renderPassSetImmediates(
        this,
        index,
        normalizeImmediateDataInput(data, dataOffset, size, 'GPURenderPassEncoder.setImmediates'),
      );
    }

    setVertexBuffer(slot, buffer, offset = 0, size) {
      this._assertOpen('GPURenderPassEncoder.setVertexBuffer');
      assertIntegerInRange(slot, 'GPURenderPassEncoder.setVertexBuffer', 'slot', UINT32_RANGE);
      assertIntegerInRange(offset, 'GPURenderPassEncoder.setVertexBuffer', 'offset', NON_NEGATIVE_RANGE);
      if (size !== undefined) {
        assertIntegerInRange(size, 'GPURenderPassEncoder.setVertexBuffer', 'size', NON_NEGATIVE_RANGE);
      }
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.setVertexBuffer', buffer)) return;
      backend.renderPassSetVertexBuffer(
        this,
        slot,
        assertLiveResource(buffer, 'GPURenderPassEncoder.setVertexBuffer', 'GPUBuffer'),
        offset,
        size,
      );
    }

    setIndexBuffer(buffer, format, offset = 0, size) {
      this._assertOpen('GPURenderPassEncoder.setIndexBuffer');
      assertIntegerInRange(offset, 'GPURenderPassEncoder.setIndexBuffer', 'offset', NON_NEGATIVE_RANGE);
      if (size !== undefined) {
        assertIntegerInRange(size, 'GPURenderPassEncoder.setIndexBuffer', 'size', NON_NEGATIVE_RANGE);
      }
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.setIndexBuffer', buffer)) return;
      backend.renderPassSetIndexBuffer(
        this,
        assertLiveResource(buffer, 'GPURenderPassEncoder.setIndexBuffer', 'GPUBuffer'),
        format,
        offset,
        size,
      );
    }

    draw(vertexCount, instanceCount = 1, firstVertex = 0, firstInstance = 0) {
      this._assertOpen('GPURenderPassEncoder.draw');
      assertIntegerInRange(vertexCount, 'GPURenderPassEncoder.draw', 'vertexCount', UINT32_RANGE);
      assertIntegerInRange(instanceCount, 'GPURenderPassEncoder.draw', 'instanceCount', UINT32_RANGE);
      assertIntegerInRange(firstVertex, 'GPURenderPassEncoder.draw', 'firstVertex', UINT32_RANGE);
      assertIntegerInRange(firstInstance, 'GPURenderPassEncoder.draw', 'firstInstance', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.draw')) return;
      backend.renderPassDraw(this, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    drawIndexed(indexCount, instanceCount = 1, firstIndex = 0, baseVertex = 0, firstInstance = 0) {
      this._assertOpen('GPURenderPassEncoder.drawIndexed');
      assertIntegerInRange(indexCount, 'GPURenderPassEncoder.drawIndexed', 'indexCount', UINT32_RANGE);
      assertIntegerInRange(instanceCount, 'GPURenderPassEncoder.drawIndexed', 'instanceCount', UINT32_RANGE);
      assertIntegerInRange(firstIndex, 'GPURenderPassEncoder.drawIndexed', 'firstIndex', UINT32_RANGE);
      assertIntegerInRange(firstInstance, 'GPURenderPassEncoder.drawIndexed', 'firstInstance', UINT32_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.drawIndexed')) return;
      backend.renderPassDrawIndexed(this, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    drawIndirect(indirectBuffer, indirectOffset = 0) {
      this._assertOpen('GPURenderPassEncoder.drawIndirect');
      assertIntegerInRange(indirectOffset, 'GPURenderPassEncoder.drawIndirect', 'indirectOffset', NON_NEGATIVE_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.drawIndirect', indirectBuffer)) return;
      backend.renderPassDrawIndirect(
        this,
        assertLiveResource(indirectBuffer, 'GPURenderPassEncoder.drawIndirect', 'GPUBuffer'),
        indirectOffset,
      );
    }

    drawIndexedIndirect(indirectBuffer, indirectOffset = 0) {
      this._assertOpen('GPURenderPassEncoder.drawIndexedIndirect');
      assertIntegerInRange(indirectOffset, 'GPURenderPassEncoder.drawIndexedIndirect', 'indirectOffset', NON_NEGATIVE_RANGE);
      if (passSkipsCommand(this, this._encoder, 'GPURenderPassEncoder.drawIndexedIndirect', indirectBuffer)) return;
      backend.renderPassDrawIndexedIndirect(
        this,
        assertLiveResource(indirectBuffer, 'GPURenderPassEncoder.drawIndexedIndirect', 'GPUBuffer'),
        indirectOffset,
      );
    }

    setViewport(x, y, width, height, minDepth, maxDepth) {
      this._assertOpen('GPURenderPassEncoder.setViewport');
      if (passInert(this)) return;
      backend.renderPassSetViewport(this, x, y, width, height, minDepth, maxDepth);
    }

    setScissorRect(x, y, width, height) {
      this._assertOpen('GPURenderPassEncoder.setScissorRect');
      assertIntegerInRange(x, 'GPURenderPassEncoder.setScissorRect', 'x', UINT32_RANGE);
      assertIntegerInRange(y, 'GPURenderPassEncoder.setScissorRect', 'y', UINT32_RANGE);
      assertIntegerInRange(width, 'GPURenderPassEncoder.setScissorRect', 'width', UINT32_RANGE);
      assertIntegerInRange(height, 'GPURenderPassEncoder.setScissorRect', 'height', UINT32_RANGE);
      if (passInert(this)) return;
      backend.renderPassSetScissorRect(this, x, y, width, height);
    }

    setBlendConstant(color) {
      this._assertOpen('GPURenderPassEncoder.setBlendConstant');
      if (color === undefined) failValidation('GPURenderPassEncoder.setBlendConstant', 'color is required');
      // GPUColor is a four-component sequence or an {r, g, b, a} dictionary.
      const { r, g, b, a } = normalizeGpuColor(color, 'GPURenderPassEncoder.setBlendConstant.color');
      if (passInert(this)) return;
      backend.renderPassSetBlendConstant(this, r, g, b, a);
    }

    setStencilReference(reference) {
      this._assertOpen('GPURenderPassEncoder.setStencilReference');
      assertIntegerInRange(reference, 'GPURenderPassEncoder.setStencilReference', 'reference', UINT32_RANGE);
      if (passInert(this)) return;
      backend.renderPassSetStencilReference(this, reference);
    }

    beginOcclusionQuery(queryIndex) {
      this._assertOpen('GPURenderPassEncoder.beginOcclusionQuery');
      assertIntegerInRange(queryIndex, 'GPURenderPassEncoder.beginOcclusionQuery', 'queryIndex', UINT32_RANGE);
      if (passInert(this)) return;
      backend.renderPassBeginOcclusionQuery(this, queryIndex);
    }

    endOcclusionQuery() {
      this._assertOpen('GPURenderPassEncoder.endOcclusionQuery');
      if (passInert(this)) return;
      backend.renderPassEndOcclusionQuery(this);
    }

    pushDebugGroup(groupLabel) {
      if (passInert(this)) return;
      backend.renderPassPushDebugGroup(this, groupLabel);
    }
    popDebugGroup() {
      if (passInert(this)) return;
      backend.renderPassPopDebugGroup(this);
    }
    insertDebugMarker(markerLabel) {
      if (passInert(this)) return;
      backend.renderPassInsertDebugMarker(this, markerLabel);
    }

    executeBundles(bundles) {
      const path = 'GPURenderPassEncoder.executeBundles';
      this._assertOpen(path);
      const list = assertArray(bundles, path, 'bundles');
      if (passSkipsCommand(this, this._encoder, path, list.find(isInvalidResource))) return;
      backend.renderPassExecuteBundles(this, list);
    }

    end() {
      this._assertOpen('GPURenderPassEncoder.end');
      if (passInert(this)) {
        this._ended = true;
        return;
      }
      backend.renderPassEnd(this);
      if (this._encoder._openPass === this) this._encoder._openPass = null;
    }
  }

  class DoeGPURenderBundle {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPURenderBundle', owner);
    }

    destroy() {
      if (typeof backend.renderBundleDestroy !== 'function') {
        return;
      }
      destroyResource(this, (native) => backend.renderBundleDestroy(native));
    }
  }

  class DoeGPURenderBundleEncoder {
    constructor(state, device) {
      this._device = device;
      this._invalidMessage = null;
      this.label = '';
      initResource(this, 'GPURenderBundleEncoder', device);
      backend.renderBundleEncoderInit(this, state);
    }

    _assertOpen(path) {
      if (isDeviceLost(this._device)) return;
      assertLiveResource(this._device, path, 'GPUDevice');
      if (typeof backend.renderBundleEncoderAssertOpen === 'function') {
        backend.renderBundleEncoderAssertOpen(this, path);
      }
    }

    setPipeline(pipeline) {
      this._assertOpen('GPURenderBundleEncoder.setPipeline');
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.setPipeline', pipeline)) return;
      backend.renderBundleEncoderSetPipeline(
        this,
        assertLiveResource(pipeline, 'GPURenderBundleEncoder.setPipeline', 'GPURenderPipeline'),
      );
    }

    setBindGroup(index, bindGroup, dynamicOffsets, dynamicOffsetsDataStart, dynamicOffsetsDataLength) {
      const path = 'GPURenderBundleEncoder.setBindGroup';
      this._assertOpen(path);
      assertIntegerInRange(index, path, 'index', UINT32_RANGE);
      const offsets = normalizeDynamicOffsets(
        path,
        arguments.length,
        dynamicOffsets,
        dynamicOffsetsDataStart,
        dynamicOffsetsDataLength,
      );
      if (passSkipsCommand(this, this, path, bindGroup)) return;
      backend.renderBundleEncoderSetBindGroup(this, index, normalizeBindGroup(bindGroup, path), offsets);
    }

    setImmediates(index, data, dataOffset = 0, size) {
      this._assertOpen('GPURenderBundleEncoder.setImmediates');
      assertIntegerInRange(index, 'GPURenderBundleEncoder.setImmediates', 'index', UINT32_RANGE);
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.setImmediates')) return;
      backend.renderBundleEncoderSetImmediates(
        this,
        index,
        normalizeImmediateDataInput(data, dataOffset, size, 'GPURenderBundleEncoder.setImmediates'),
      );
    }

    setVertexBuffer(slot, buffer, offset = 0, size) {
      this._assertOpen('GPURenderBundleEncoder.setVertexBuffer');
      assertIntegerInRange(slot, 'GPURenderBundleEncoder.setVertexBuffer', 'slot', UINT32_RANGE);
      assertIntegerInRange(offset, 'GPURenderBundleEncoder.setVertexBuffer', 'offset', NON_NEGATIVE_RANGE);
      if (size !== undefined) {
        assertIntegerInRange(size, 'GPURenderBundleEncoder.setVertexBuffer', 'size', NON_NEGATIVE_RANGE);
      }
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.setVertexBuffer', buffer)) return;
      backend.renderBundleEncoderSetVertexBuffer(
        this,
        slot,
        assertLiveResource(buffer, 'GPURenderBundleEncoder.setVertexBuffer', 'GPUBuffer'),
        offset,
        size,
      );
    }

    setIndexBuffer(buffer, format, offset = 0, size) {
      this._assertOpen('GPURenderBundleEncoder.setIndexBuffer');
      assertIntegerInRange(offset, 'GPURenderBundleEncoder.setIndexBuffer', 'offset', NON_NEGATIVE_RANGE);
      if (size !== undefined) {
        assertIntegerInRange(size, 'GPURenderBundleEncoder.setIndexBuffer', 'size', NON_NEGATIVE_RANGE);
      }
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.setIndexBuffer', buffer)) return;
      backend.renderBundleEncoderSetIndexBuffer(
        this,
        assertLiveResource(buffer, 'GPURenderBundleEncoder.setIndexBuffer', 'GPUBuffer'),
        format,
        offset,
        size,
      );
    }

    draw(vertexCount, instanceCount = 1, firstVertex = 0, firstInstance = 0) {
      this._assertOpen('GPURenderBundleEncoder.draw');
      assertIntegerInRange(vertexCount, 'GPURenderBundleEncoder.draw', 'vertexCount', UINT32_RANGE);
      assertIntegerInRange(instanceCount, 'GPURenderBundleEncoder.draw', 'instanceCount', UINT32_RANGE);
      assertIntegerInRange(firstVertex, 'GPURenderBundleEncoder.draw', 'firstVertex', UINT32_RANGE);
      assertIntegerInRange(firstInstance, 'GPURenderBundleEncoder.draw', 'firstInstance', UINT32_RANGE);
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.draw')) return;
      backend.renderBundleEncoderDraw(this, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    drawIndexed(indexCount, instanceCount = 1, firstIndex = 0, baseVertex = 0, firstInstance = 0) {
      this._assertOpen('GPURenderBundleEncoder.drawIndexed');
      assertIntegerInRange(indexCount, 'GPURenderBundleEncoder.drawIndexed', 'indexCount', UINT32_RANGE);
      assertIntegerInRange(instanceCount, 'GPURenderBundleEncoder.drawIndexed', 'instanceCount', UINT32_RANGE);
      assertIntegerInRange(firstIndex, 'GPURenderBundleEncoder.drawIndexed', 'firstIndex', UINT32_RANGE);
      assertIntegerInRange(firstInstance, 'GPURenderBundleEncoder.drawIndexed', 'firstInstance', UINT32_RANGE);
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.drawIndexed')) return;
      backend.renderBundleEncoderDrawIndexed(this, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    drawIndirect(indirectBuffer, indirectOffset = 0) {
      this._assertOpen('GPURenderBundleEncoder.drawIndirect');
      assertIntegerInRange(indirectOffset, 'GPURenderBundleEncoder.drawIndirect', 'indirectOffset', NON_NEGATIVE_RANGE);
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.drawIndirect', indirectBuffer)) return;
      backend.renderBundleEncoderDrawIndirect(
        this,
        assertLiveResource(indirectBuffer, 'GPURenderBundleEncoder.drawIndirect', 'GPUBuffer'),
        indirectOffset,
      );
    }

    drawIndexedIndirect(indirectBuffer, indirectOffset = 0) {
      this._assertOpen('GPURenderBundleEncoder.drawIndexedIndirect');
      assertIntegerInRange(indirectOffset, 'GPURenderBundleEncoder.drawIndexedIndirect', 'indirectOffset', NON_NEGATIVE_RANGE);
      if (passSkipsCommand(this, this, 'GPURenderBundleEncoder.drawIndexedIndirect', indirectBuffer)) return;
      backend.renderBundleEncoderDrawIndexedIndirect(
        this,
        assertLiveResource(indirectBuffer, 'GPURenderBundleEncoder.drawIndexedIndirect', 'GPUBuffer'),
        indirectOffset,
      );
    }

    pushDebugGroup(groupLabel) {
      if (this._finished && passSkipsCommand(this, this, 'GPURenderBundleEncoder.pushDebugGroup')) return;
      if (passInert(this)) return;
      backend.renderBundleEncoderPushDebugGroup(this, groupLabel);
    }
    popDebugGroup() {
      if (this._finished && passSkipsCommand(this, this, 'GPURenderBundleEncoder.popDebugGroup')) return;
      if (passInert(this)) return;
      backend.renderBundleEncoderPopDebugGroup(this);
    }
    insertDebugMarker(markerLabel) {
      if (this._finished && passSkipsCommand(this, this, 'GPURenderBundleEncoder.insertDebugMarker')) return;
      if (passInert(this)) return;
      backend.renderBundleEncoderInsertDebugMarker(this, markerLabel);
    }

    finish(descriptor) {
      this._assertOpen('GPURenderBundleEncoder.finish');
      const device = this._device;
      if (this._finished && !isDeviceLost(device)) {
        deliverDeviceError(device, 'validation', new GPUValidationError('GPURenderBundleEncoder.finish: the render bundle encoder is already finished'));
        return markInvalid(new DoeGPURenderBundle(null, device), descriptor?.label);
      }
      if (isDeviceLost(device)) {
        this._finished = true;
        return markInvalid(new DoeGPURenderBundle(null, device), descriptor?.label);
      }
      const invalid = this._invalidMessage != null;
      const { result, errors } = collectDeviceErrors(device, () => {
        if (!invalid) {
          return backend.renderBundleEncoderFinish(
            this,
            descriptor === undefined ? undefined : assertObject(descriptor, 'GPURenderBundleEncoder.finish', 'descriptor'),
            classes,
          );
        }
        // An invalid encoder's native recording, if any, is finished and dropped.
        if (!this._invalid) {
          backend.renderBundleEncoderFinish(this, undefined, classes).destroy();
        }
        return null;
      });
      this._finished = true;
      reportFinishError(device, errors, this._invalidMessage);
      if (invalid || errors.length > 0) {
        result?.destroy?.();
        return markInvalid(new DoeGPURenderBundle(null, device), descriptor?.label);
      }
      result.label = descriptor?.label ?? '';
      return result;
    }
  }

  class DoeGPUCommandBuffer {
    constructor(state, owner) {
      this._batched = state?._batched === true;
      this._commands = Array.isArray(state?._commands) ? state._commands : [];
      this._native = this._batched ? null : (state?._native ?? null);
      this._submitted = false;
      this._finalizerToken = null;
      this.label = '';
      initResource(this, 'GPUCommandBuffer', owner);
      if (this._native != null && commandBufferFinalizer) {
        this._finalizerToken = {};
        commandBufferFinalizer.register(this, this._native, this._finalizerToken);
      }
    }

    destroy() {
      releaseCommandBuffer(this);
    }
  }

  class DoeGPUCommandEncoder {
    constructor(state, device) {
      this._device = device;
      this._invalidMessage = null;
      this._openPass = null;
      this.label = '';
      initResource(this, 'GPUCommandEncoder', device);
      backend.commandEncoderInit(this, state);
    }

    _assertOpen(path) {
      if (isDeviceLost(this._device)) return;
      assertLiveResource(this._device, path, 'GPUDevice');
      if (typeof backend.commandEncoderAssertOpen === 'function') {
        backend.commandEncoderAssertOpen(this, path);
      }
    }

    beginComputePass(descriptor) {
      const path = 'GPUCommandEncoder.beginComputePass';
      this._assertOpen(path);
      if (encoderSkipsUse(this, path, descriptor?.timestampWrites?.querySet)) {
        const invalidPass = new DoeGPUComputePassEncoder(null, this);
        invalidPass._invalid = true;
        invalidPass.label = descriptor?.label ?? '';
        return invalidPass;
      }
      const pass = backend.commandEncoderBeginComputePass(this, descriptor, classes);
      pass.label = descriptor?.label ?? '';
      this._openPass = pass;
      return pass;
    }

    beginRenderPass(descriptor) {
      this._assertOpen('GPUCommandEncoder.beginRenderPass');
      const passDescriptor = assertObject(descriptor, 'GPUCommandEncoder.beginRenderPass', 'descriptor');
      const named = renderPassDescriptorObjects(passDescriptor);
      // A GPUTexture attachment stands for its default view; an invalid one
      // must not reach createView, which would report a second error.
      if (encoderSkipsUse(this, 'GPUCommandEncoder.beginRenderPass', named.find(isInvalidResource))) {
        const invalidPass = new DoeGPURenderPassEncoder(null, this);
        invalidPass._invalid = true;
        invalidPass.label = passDescriptor.label ?? '';
        return invalidPass;
      }
      const pass = backend.commandEncoderBeginRenderPass(this, passDescriptor, classes);
      pass.label = passDescriptor.label ?? '';
      this._openPass = pass;
      return pass;
    }

    copyBufferToBuffer(src, srcOffsetOrDst, dstOrSize, dstOffsetOrUndefined, sizeOrUndefined) {
      this._assertOpen('GPUCommandEncoder.copyBufferToBuffer');
      const srcBuffer = src;
      const useNewSignature = typeof srcOffsetOrDst === 'object' && srcOffsetOrDst !== null;
      const srcOffset = useNewSignature ? 0 : srcOffsetOrDst;
      const dst = useNewSignature ? srcOffsetOrDst : dstOrSize;
      const dstOffset = useNewSignature ? 0 : dstOffsetOrUndefined;
      const explicitSize = useNewSignature ? dstOrSize : sizeOrUndefined;
      assertIntegerInRange(srcOffset, 'GPUCommandEncoder.copyBufferToBuffer', 'srcOffset', NON_NEGATIVE_RANGE);
      assertIntegerInRange(dstOffset, 'GPUCommandEncoder.copyBufferToBuffer', 'dstOffset', NON_NEGATIVE_RANGE);
      if (encoderSkipsUse(this, 'GPUCommandEncoder.copyBufferToBuffer', srcBuffer, dst)) return;
      const srcNative = assertLiveResource(srcBuffer, 'GPUCommandEncoder.copyBufferToBuffer', 'GPUBuffer');
      const dstNative = assertLiveResource(dst, 'GPUCommandEncoder.copyBufferToBuffer', 'GPUBuffer');
      const size = explicitSize === undefined ? Math.max(0, srcBuffer.size - srcOffset) : explicitSize;
      assertIntegerInRange(size, 'GPUCommandEncoder.copyBufferToBuffer', 'size', NON_NEGATIVE_RANGE);
      backend.commandEncoderCopyBufferToBuffer(
        this,
        srcNative,
        srcOffset,
        dstNative,
        dstOffset,
        size,
      );
    }

    copyBufferToTexture(source, destination, copySize) {
      const path = 'GPUCommandEncoder.copyBufferToTexture';
      this._assertOpen(path);
      const sourceObject = assertObject(source, 'GPUCommandEncoder.copyBufferToTexture', 'source');
      const destinationObject = assertObject(destination, 'GPUCommandEncoder.copyBufferToTexture', 'destination');
      const sizeObject = normalizeCopySize(copySize, 'GPUCommandEncoder.copyBufferToTexture');
      assertIntegerInRange(sourceObject.offset ?? 0, 'GPUCommandEncoder.copyBufferToTexture', 'source.offset', NON_NEGATIVE_RANGE);
      assertIntegerInRange(sizeObject.width, 'GPUCommandEncoder.copyBufferToTexture', 'copySize.width', UINT32_RANGE);
      assertIntegerInRange(sizeObject.height, 'GPUCommandEncoder.copyBufferToTexture', 'copySize.height', UINT32_RANGE);
      if (sizeObject.depthOrArrayLayers !== undefined) {
        assertIntegerInRange(sizeObject.depthOrArrayLayers, 'GPUCommandEncoder.copyBufferToTexture', 'copySize.depthOrArrayLayers', UINT32_RANGE);
      }
      if (encoderSkipsUse(this, path, sourceObject.buffer, destinationObject.texture)) return;
      backend.commandEncoderCopyBufferToTexture(
        this,
        {
          buffer: assertLiveResource(sourceObject.buffer, 'GPUCommandEncoder.copyBufferToTexture', 'GPUBuffer'),
          offset: sourceObject.offset ?? 0,
          bytesPerRow: assertOptionalStride(sourceObject.bytesPerRow, path, 'source.bytesPerRow'),
          rowsPerImage: assertOptionalStride(sourceObject.rowsPerImage, path, 'source.rowsPerImage'),
        },
        {
          texture: assertLiveResource(destinationObject.texture, 'GPUCommandEncoder.copyBufferToTexture', 'GPUTexture'),
          mipLevel: destinationObject.mipLevel ?? 0,
          origin: normalizeOrigin3D(destinationObject.origin, path, 'destination.origin'),
          aspect: destinationObject.aspect,
        },
        {
          width: sizeObject.width,
          height: sizeObject.height,
          depthOrArrayLayers: sizeObject.depthOrArrayLayers ?? 1,
        },
      );
    }

    copyTextureToBuffer(source, destination, copySize) {
      const path = 'GPUCommandEncoder.copyTextureToBuffer';
      this._assertOpen(path);
      const sourceObject = assertObject(source, 'GPUCommandEncoder.copyTextureToBuffer', 'source');
      const destinationObject = assertObject(destination, 'GPUCommandEncoder.copyTextureToBuffer', 'destination');
      const sizeObject = normalizeCopySize(copySize, 'GPUCommandEncoder.copyTextureToBuffer');
      assertIntegerInRange(destinationObject.offset ?? 0, 'GPUCommandEncoder.copyTextureToBuffer', 'destination.offset', NON_NEGATIVE_RANGE);
      assertIntegerInRange(sizeObject.width, 'GPUCommandEncoder.copyTextureToBuffer', 'copySize.width', UINT32_RANGE);
      assertIntegerInRange(sizeObject.height, 'GPUCommandEncoder.copyTextureToBuffer', 'copySize.height', UINT32_RANGE);
      if (sizeObject.depthOrArrayLayers !== undefined) {
        assertIntegerInRange(sizeObject.depthOrArrayLayers, 'GPUCommandEncoder.copyTextureToBuffer', 'copySize.depthOrArrayLayers', UINT32_RANGE);
      }
      if (encoderSkipsUse(this, path, sourceObject.texture, destinationObject.buffer)) return;
      backend.commandEncoderCopyTextureToBuffer(
        this,
        {
          texture: assertLiveResource(sourceObject.texture, 'GPUCommandEncoder.copyTextureToBuffer', 'GPUTexture'),
          mipLevel: sourceObject.mipLevel ?? 0,
          origin: normalizeOrigin3D(sourceObject.origin, path, 'source.origin'),
          aspect: sourceObject.aspect,
        },
        {
          buffer: assertLiveResource(destinationObject.buffer, 'GPUCommandEncoder.copyTextureToBuffer', 'GPUBuffer'),
          offset: destinationObject.offset ?? 0,
          bytesPerRow: assertOptionalStride(destinationObject.bytesPerRow, path, 'destination.bytesPerRow'),
          rowsPerImage: assertOptionalStride(destinationObject.rowsPerImage, path, 'destination.rowsPerImage'),
        },
        {
          width: sizeObject.width,
          height: sizeObject.height,
          depthOrArrayLayers: sizeObject.depthOrArrayLayers ?? 1,
        },
      );
    }

    copyTextureToTexture(source, destination, copySize) {
      const path = 'GPUCommandEncoder.copyTextureToTexture';
      this._assertOpen(path);
      const sourceObject = assertObject(source, 'GPUCommandEncoder.copyTextureToTexture', 'source');
      const destinationObject = assertObject(destination, 'GPUCommandEncoder.copyTextureToTexture', 'destination');
      const sizeObject = normalizeCopySize(copySize, 'GPUCommandEncoder.copyTextureToTexture');
      if (encoderSkipsUse(this, path, sourceObject.texture, destinationObject.texture)) return;
      backend.commandEncoderCopyTextureToTexture(
        this,
        {
          texture: assertLiveResource(sourceObject.texture, 'GPUCommandEncoder.copyTextureToTexture', 'GPUTexture'),
          mipLevel: sourceObject.mipLevel ?? 0,
          origin: normalizeOrigin3D(sourceObject.origin, path, 'source.origin'),
          aspect: sourceObject.aspect,
        },
        {
          texture: assertLiveResource(destinationObject.texture, 'GPUCommandEncoder.copyTextureToTexture', 'GPUTexture'),
          mipLevel: destinationObject.mipLevel ?? 0,
          origin: normalizeOrigin3D(destinationObject.origin, path, 'destination.origin'),
          aspect: destinationObject.aspect,
        },
        {
          width: sizeObject.width,
          height: sizeObject.height,
          depthOrArrayLayers: sizeObject.depthOrArrayLayers ?? 1,
        },
      );
    }

    clearBuffer(buffer, offset = 0, size) {
      this._assertOpen('GPUCommandEncoder.clearBuffer');
      assertIntegerInRange(offset, 'GPUCommandEncoder.clearBuffer', 'offset', NON_NEGATIVE_RANGE);
      if (size !== undefined) {
        assertIntegerInRange(size, 'GPUCommandEncoder.clearBuffer', 'size', NON_NEGATIVE_RANGE);
      }
      if (encoderSkipsUse(this, 'GPUCommandEncoder.clearBuffer', buffer)) return;
      backend.commandEncoderClearBuffer(
        this,
        assertLiveResource(buffer, 'GPUCommandEncoder.clearBuffer', 'GPUBuffer'),
        offset,
        size,
      );
    }

    writeTimestamp(querySet, queryIndex) {
      this._assertOpen('GPUCommandEncoder.writeTimestamp');
      if (encoderSkipsUse(this, 'GPUCommandEncoder.writeTimestamp', querySet)) return;
      const querySetNative = assertLiveResource(querySet, 'GPUCommandEncoder.writeTimestamp', 'GPUQuerySet');
      assertIntegerInRange(queryIndex, 'GPUCommandEncoder.writeTimestamp', 'queryIndex', UINT32_RANGE);
      if (queryIndex >= querySet.count) {
        failValidation('GPUCommandEncoder.writeTimestamp', `queryIndex ${queryIndex} exceeds querySet count ${querySet.count}`);
      }
      backend.commandEncoderWriteTimestamp(this, querySetNative, queryIndex);
    }

    resolveQuerySet(querySet, firstQuery, queryCount, destination, destinationOffset) {
      this._assertOpen('GPUCommandEncoder.resolveQuerySet');
      if (encoderSkipsUse(this, 'GPUCommandEncoder.resolveQuerySet', querySet, destination)) return;
      const querySetNative = assertLiveResource(querySet, 'GPUCommandEncoder.resolveQuerySet', 'GPUQuerySet');
      assertIntegerInRange(firstQuery, 'GPUCommandEncoder.resolveQuerySet', 'firstQuery', UINT32_RANGE);
      assertIntegerInRange(queryCount, 'GPUCommandEncoder.resolveQuerySet', 'queryCount', POSITIVE_UINT32_RANGE);
      if (firstQuery + queryCount > querySet.count) {
        failValidation('GPUCommandEncoder.resolveQuerySet', `firstQuery ${firstQuery} + queryCount ${queryCount} exceeds querySet count ${querySet.count}`);
      }
      const destinationNative = assertLiveResource(destination, 'GPUCommandEncoder.resolveQuerySet', 'GPUBuffer');
      assertIntegerInRange(destinationOffset, 'GPUCommandEncoder.resolveQuerySet', 'destinationOffset', NON_NEGATIVE_RANGE);
      backend.commandEncoderResolveQuerySet(this, querySetNative, firstQuery, queryCount, destinationNative, destinationOffset);
    }

    pushDebugGroup(groupLabel) {
      if (isDeviceLost(this._device)) return;
      backend.commandEncoderPushDebugGroup(this, groupLabel);
    }
    popDebugGroup() {
      if (isDeviceLost(this._device)) return;
      backend.commandEncoderPopDebugGroup(this);
    }
    insertDebugMarker(markerLabel) {
      if (isDeviceLost(this._device)) return;
      backend.commandEncoderInsertDebugMarker(this, markerLabel);
    }

    finish(descriptor) {
      const device = this._device;
      if (this._openPass !== null && !this._openPass._ended && !this._finished && !isDeviceLost(device)) {
        // A locked encoder (a pass still open) finishes into an invalid
        // command buffer with a validation error; it records nothing more.
        this._finished = true;
        deliverDeviceError(device, 'validation', new GPUValidationError('GPUCommandEncoder.finish: the command encoder has an open pass'));
        return markInvalid(new DoeGPUCommandBuffer({ _batched: true, _commands: [] }, device), descriptor?.label);
      }
      this._assertOpen('GPUCommandEncoder.finish');
      if (isDeviceLost(device)) {
        // A lost device's encoder finishes into an invalid command buffer
        // without touching the provider; no error is generated.
        this._finished = true;
        return markInvalid(new DoeGPUCommandBuffer({ _batched: true, _commands: [] }, device), descriptor?.label);
      }
      const { result: state, errors } = collectDeviceErrors(device, () => backend.commandEncoderFinish(this));
      reportFinishError(device, errors, this._invalidMessage);
      if (this._invalidMessage == null && errors.length === 0) {
        const cmdBuf = new classes.DoeGPUCommandBuffer(state, device);
        cmdBuf.label = descriptor?.label ?? '';
        return cmdBuf;
      }
      // An encoder that finished with an error, or was invalidated by an
      // invalid object, yields an invalid command buffer and drops its native
      // recording.
      if (state?._native != null && typeof backend.commandBufferDestroy === 'function') {
        backend.commandBufferDestroy(state._native);
      }
      return markInvalid(new DoeGPUCommandBuffer({ _batched: true, _commands: [] }, device), descriptor?.label);
    }
  }

  classes = {
    DoeGPUComputePassEncoder,
    DoeGPURenderPassEncoder,
    DoeGPURenderBundle,
    DoeGPURenderBundleEncoder,
    DoeGPUCommandBuffer,
    DoeGPUCommandEncoder,
  };
  return classes;
}

export {
  createEncoderClasses,
};
