#!/usr/bin/env node
// Reports the adapter features and limits one provider exposes for the
// requested backend, through the same provider loading and adapter selection
// the scoreboard executors use. The runner probes both providers in fresh
// processes and intersects the results for intersection-mode hosts.
import { parseArgs } from 'node:util';

import { BACKENDS, PROVIDERS, limitsRecord, loadProvider } from './common.js';

const { values } = parseArgs({
  options: { provider: { type: 'string' }, backend: { type: 'string' } },
  allowPositionals: false,
});
if (!PROVIDERS.includes(values.provider) || !BACKENDS.includes(values.backend)) {
  process.stderr.write(`usage: probe-capabilities.js --provider ${PROVIDERS.join('|')} --backend ${BACKENDS.join('|')}\n`);
  process.exit(2);
}
const provider = await loadProvider(values.provider, values.backend);
const gpu = provider.module.create(provider.contract.createOptions);
const adapter = await gpu.requestAdapter({ powerPreference: 'high-performance', ...provider.contract.adapterOptions });
if (!adapter) {
  process.stderr.write(`${values.provider} returned no ${values.backend} adapter\n`);
  process.exit(1);
}
const info = adapter.info ?? {};
process.stdout.write(`${JSON.stringify({
  provider: values.provider,
  backend: values.backend,
  adapterInfo: {
    vendor: info.vendor ?? '', architecture: info.architecture ?? '', device: info.device ?? '',
    description: info.description ?? '', backendType: Number.isInteger(info.backendType) ? info.backendType : null,
    isFallbackAdapter: info.isFallbackAdapter === true,
  },
  features: [...adapter.features].sort(),
  limits: limitsRecord(adapter.limits),
})}\n`);
process.exit(0);
