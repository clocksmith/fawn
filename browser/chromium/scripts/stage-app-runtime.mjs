// Immutable app-local runtime closure. Never overwrite a library already mapped by a GPU process.
import { createHash } from 'node:crypto';
import { basename, dirname, join, relative, resolve } from 'node:path';
import { copyFileSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readFileSync, realpathSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
export function stageAppRuntime({ app, library }) {
  const names = [basename(library), ...(basename(library) === 'libwebgpu_doe_c.dylib' ? ['libdoe_compiler.dylib'] : [])];
  const files = names.map(name => {
    const source = realpathSync(join(dirname(library), name)), bytes = readFileSync(source);
    if (!lstatSync(source).isFile()) throw Error(`Runtime closure requires a regular file: ${name}`);
    return { source, name, sha256: digest(bytes), byteLength: bytes.length };
  });
  const manifest = { schemaVersion: 1, files: files.map(({ source, ...value }) => value) };
  const identity = digest(JSON.stringify(manifest)), frameworks = join(resolve(app), 'Contents', 'Frameworks');
  const destination = join(frameworks, `doe-runtime-${identity}`);
  mkdirSync(frameworks, { recursive: true });
  if (existsSync(destination)) {
    if (!lstatSync(destination).isDirectory() || lstatSync(destination).isSymbolicLink()) throw Error('Invalid existing runtime bundle');
    for (const file of files) {
      const path = join(destination, file.name);
      if (!lstatSync(path).isFile() || digest(readFileSync(path)) !== file.sha256) throw Error('Existing runtime bundle does not match its identity');
    }
  } else {
    const temporary = mkdtempSync(join(frameworks, '.doe-runtime-candidate-'));
    try {
      for (const file of files) {
        const path = join(temporary, file.name); copyFileSync(file.source, path);
        if (digest(readFileSync(path)) !== file.sha256) throw Error('Runtime source changed during preparation');
      }
      writeFileSync(join(temporary, 'bundle.json'), `${JSON.stringify(manifest, null, 2)}\n`);
      renameSync(temporary, destination);
    } finally { rmSync(temporary, { recursive: true, force: true }); }
  }
  return { path: join(destination, basename(library)), relativePath: relative(frameworks, join(destination, basename(library))), identity };
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.length !== 4 || args[0] !== '--app' || args[2] !== '--library') throw Error('Usage: stage-app-runtime.mjs --app PATH --library PATH');
  console.log(stageAppRuntime({ app: args[1], library: args[3] }).path);
}
