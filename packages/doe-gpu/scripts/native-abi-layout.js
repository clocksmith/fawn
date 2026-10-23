// Compare bridge layouts with the same pinned C header used by the Zig runtime.
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';

// Bridge mirrors whose names differ from the header's.
const RENAMED_LAYOUTS = [
  ['WGPUComputePassTimestampWrites', 'WGPUPassTimestampWrites',
    ['nextInChain', 'querySet', 'beginningOfPassWriteIndex', 'endOfPassWriteIndex']],
  ['WGPURenderPassTimestampWrites', 'WGPUPassTimestampWrites',
    ['nextInChain', 'querySet', 'beginningOfPassWriteIndex', 'endOfPassWriteIndex']],
];
// Trailing members a same-named mirror may add after every header member. A
// standard provider never reads them, so each needs a stated consumer.
const TRAILING_EXTRAS = Object.freeze({
  WGPUShaderModuleDescriptor: ['compilationHintCount', 'compilationHints'], // Zig doeNative shader entry
  WGPUTextureDescriptor: ['textureBindingViewDimension'], // Zig doeNative texture entry
});

function structFields(body) {
  const names = [];
  const source = body.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  for (const declaration of source.split(';').map((text) => text.trim()).filter(Boolean)) {
    const callback = declaration.match(/\(\s*\*\s*(\w+)\s*\)/);
    if (callback) {
      names.push(callback[1]);
      continue;
    }
    for (const part of declaration.split(',')) {
      const name = part.match(/(\w+)\s*(\[[^\]]*\])?\s*$/);
      if (name) names.push(name[1]);
    }
  }
  return names;
}

// Every bridge typedef that shares a webgpu.h struct name must list the
// header's members in order; listed trailing extras are the only additions.
function sameNamedLayouts(bridgeHeader, upstreamHeader) {
  const bridge = readFileSync(bridgeHeader, 'utf8');
  const upstream = readFileSync(upstreamHeader, 'utf8');
  const layouts = [];
  for (const match of bridge.matchAll(/typedef struct(?:\s+\w+)?\s*\{([^{}]*)\}\s*(WGPU\w+)\s*;/g)) {
    const name = match[2];
    const pinned = upstream.match(new RegExp(`typedef struct ${name} \\{([\\s\\S]*?)\\} ${name} WGPU_STRUCTURE_ATTRIBUTE`));
    if (!pinned) continue;
    const fields = structFields(match[1]);
    const headerFields = structFields(pinned[1]);
    const extras = TRAILING_EXTRAS[name] ?? [];
    const expected = [...headerFields, ...extras];
    if (fields.join(',') !== expected.join(',')) {
      throw new Error(`Native ABI mirror ${name} has members [${fields}]; webgpu.h has [${headerFields}]${extras.length ? ` plus trailing [${extras}]` : ''}`);
    }
    layouts.push([name, name, headerFields, extras.length === 0]);
  }
  return layouts;
}

function checkNativeAbiLayouts({ compiler, prefixArgs = [], includeDir, bridgeHeader, upstreamHeader }) {
  const layouts = [
    ...RENAMED_LAYOUTS.map((layout) => [...layout, true]),
    ...sameNamedLayouts(bridgeHeader, upstreamHeader),
  ];
  const directory = mkdtempSync(join(tmpdir(), 'doe-native-abi-'));
  try {
    const observed = [bridgeHeader, upstreamHeader].map((header, side) => {
      const output = join(directory, `layout-${side}${process.platform === 'win32' ? '.exe' : ''}`);
      const rows = layouts.map((layout) => {
        const name = layout[side];
        const values = [...(layout[3] ? [`sizeof(${name})`] : []), ...layout[2].map((field) => `offsetof(${name}, ${field})`)];
        return `printf("${values.map(() => '%zu').join(' ')}\\n", ${values.join(', ')});`;
      });
      const source = `#include <stdio.h>\n#include <stddef.h>\n#include ${JSON.stringify(header)}\nint main(void) { ${rows.join('\n')} return 0; }\n`;
      execFileSync(compiler, [...prefixArgs, '-std=c11', `-I${includeDir}`, '-x', 'c', '-', '-o', output], { input: source, stdio: ['pipe', 'pipe', 'pipe'] });
      return execFileSync(output, { encoding: 'utf8' }).trim().split(/\r?\n/);
    });
    for (const [index, layout] of layouts.entries()) {
      if (observed[0][index] !== observed[1][index]) {
        throw new Error(`Native ABI mismatch for ${layout[0]}: bridge ${observed[0][index]}; pinned header ${observed[1][index]}`);
      }
    }
    return layouts.length;
  } finally { rmSync(directory, { recursive: true, force: true }); }
}

export { checkNativeAbiLayouts };
