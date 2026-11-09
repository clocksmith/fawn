import assert from 'node:assert/strict';
import { createNativeDirect } from '../../src/native.js';

// One lifecycle owner for comparing the two descriptor entrypoints.
export function createDeviceDescriptorEntryPoints(addon) {
  const nativeGpu = createNativeDirect();
  let adapterInfo = null;
  const entryPoints = [
    {
      name: 'standard',
      request: async (desc) => {
        const inst = addon.createInstance();
        try {
          const rawAdapter = addon.requestAdapter(inst);
          assert.ok(rawAdapter, 'descriptor parity requires a real adapter');
          try {
            adapterInfo ??= addon.adapterGetInfo(rawAdapter);
            const rawDevice = addon.requestDevice(inst, rawAdapter, desc);
            try {
              return addon.deviceGetLimits(rawDevice);
            } finally {
              addon.deviceRelease(rawDevice);
            }
          } finally {
            addon.adapterRelease(rawAdapter);
          }
        } finally {
          addon.instanceRelease(inst);
        }
      },
    },
    {
      name: 'native-direct',
      request: async (desc) => {
        const nextAdapter = await nativeGpu.requestAdapter();
        assert.ok(nextAdapter, 'native-direct descriptor parity requires a real adapter');
        try {
          const nextDevice = await nextAdapter.requestDevice(desc);
          try {
            return nextDevice.limits;
          } finally {
            nextDevice.destroy?.();
          }
        } finally {
          nextAdapter.destroy?.();
        }
      },
    },
  ];
  return { entryPoints, getAdapterInfo: () => adapterInfo };
}
