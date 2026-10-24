# Doe JavaScript style guide

This guide is the JavaScript style contract for `packages/doe-gpu`.

## Core principles

- Pure ESM throughout. No CommonJS, no dynamic `require()`.
- Runtime validation over static types; JSDoc for public API intent.
- Fail fast with path-based error context.
- The checked-in `src/vendor/` is maintained source; its name grants no review exemption.
- WGSL execution belongs to the native compiler and runtime. Never replace shader
  work with host results selected from source patterns, names, or comments.
- Readback obtains native resource contents. Host staging may copy mapped bytes
  for JavaScript access, but cannot simulate execution or become a readback oracle.

## Modules

- All modules are ESM via `"type": "module"` in `package.json`.
- Use named imports with the `.js` extension: `import { x, y } from './module.js'`.
- Avoid new default exports in authored internal modules. Vendor-synced
  compatibility modules may keep upstream defaults; package entrypoints may use
  named or default exports when that keeps the public surface explicit.

## File naming

- `kebab-case.js` for all source files; no `.mjs` (ESM is declared in
  `package.json`).
- Test files: `test-<scope>.js` in `test/smoke/` and `test/integration/`
  (e.g. `test-smoke-load.js`, `test-integration-gpu-namespace.js`);
  `<scope>.test.js` in `test/unit/`.
- Runtime shims: named by runtime (`bun.js`, `deno.js`, `browser.js`).

## Naming

- Functions, methods, variables, and properties: `camelCase`
- Boolean predicates: `is`/`has`/`should` prefix (`isBCFormat()`,
  `hasStencilAspect()`)
- Classes: `PascalCase`
- Constants: `UPPER_SNAKE_CASE` (`UINT32_MAX`, `ALL_BUFFER_USAGE_BITS`)
- WeakMap metadata keys: `UPPER_SNAKE_CASE` (`DOE_BUFFER_META`,
  `DOE_PENDING_ENCODERS`)
- Private/internal properties: leading underscore (`_native`, `_destroyed`)
- WebGPU enum values: kebab-case strings matching the spec (`'read-only-storage'`,
  `'non-filtering'`)

## Constants

- Use `Object.freeze()` for enum-like lookup objects.
- No bare magic numbers. Name all thresholds and limits.
- Place constants at file top, after imports.

## Exports

- Prefer named exports grouped at the end of authored modules; small facades,
  package entrypoints, and vendor-synced modules may export inline when clearer.
- Re-export barrels use `export * from` or `export { name } from`.

## Error handling

- Use `failValidation(path, message)` for WebGPU validation errors.
- Include descriptor path context: `descriptor.entries[${i}].buffer.type`.
- Throw `Error` directly; do not subclass unless the caller needs to
  discriminate.
- Attaching stable machine-readable fields to `Error` objects is acceptable
  when callers need structured diagnostics.
- Use try-catch only for expected failure modes (import probing, optional
  features).

## Validation

- Validate at API boundaries using runtime type checks.
- JSON schema, runtime validator field tables, and `.d.ts` declarations agree.
  When you change the plan/capture contract, run `npm run check:plan-schema`.
- Validation failures expose stable `code`, `path`, `expected`, and `received`
  fields in addition to a readable message.
- Use helper functions (`assertObject`, `assertArray`, `assertBoolean`,
  `assertNonEmptyString`, `assertIntegerInRange`, `assertOptionalIntegerInRange`,
  `normalizeEnumKey`, and the format-specific normalizers in `validation.js`)
  rather than inline `typeof` checks.
- Nullish coalescing for optional descriptor fields:
  `descriptor?.label ?? ''`.

## Comments

- File header: 1-3 line `//` comment identifying the module.
- Section separators for large files: `// === Section name ===`.
- JSDoc `/** */` blocks for public API functions and classes.
- Inline `//` comments only for non-obvious constraints or rationale.
- No TODO/FIXME inline; track follow-ups in the status log (`docs/status.md`, with dated entries in the current `docs/status/*.md` shard).

```javascript
// doe-gpu — compute surface
//
// Narrower entrypoint for compute-only workloads.
```

## Formatting

- 2-space indentation.
- Semicolons required.
- Line length: prefer under 100 characters.
- Prefer single quotes in authored package code. Follow the surrounding file's
  existing quote style in vendor-synced or generated modules.
- Trailing commas in array/object literals, not function parameter lists.

## Testing

- No test framework. Vanilla Node.js assertions with `check(label, condition,
  detail?)` helpers.
- Smoke tests: sequential checks, exit 0/1, output `ok:` or `FAIL:` lines.
- Integration tests: discovered by pattern (`test-integration-*.js`), spawned
  as subprocesses; most need the native addon and a GPU adapter.
- Run the touched test file with `node`. `npm test` chains contracts and smoke;
  native integration is `npm run test:integration`. `prepublishOnly` runs both;
  neither is a default development gate.
- `test/unit/js-style.test.js` (in `npm run test:contracts`) enforces the
  mechanical rules above: file naming, `.js` only, no `require()` outside the
  addon loader, no TODO/FIXME. The native addon's rules live in
  [its AGENTS.md](../../runtime/bridge/webgpu-addon/AGENTS.md).

## Dependencies

- Zero runtime dependencies in the published package.
- Dev tooling uses Node.js built-ins only (`child_process`, `path`, `fs`).
- No bundler, no transpiler. Ship source JS directly.

## Scripts

- CLI scripts start with `#!/usr/bin/env node`.
- Scripts live in `scripts/` and are referenced from `package.json`.
