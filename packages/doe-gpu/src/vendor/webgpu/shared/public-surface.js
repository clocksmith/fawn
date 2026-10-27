function setupGlobalsOnTarget(target, gpu, globals) {
  for (const [name, value] of Object.entries(globals)) {
    if (target[name] === undefined) {
      Object.defineProperty(target, name, {
        value,
        writable: true,
        configurable: true,
        enumerable: false,
      });
    }
  }
  if (typeof target.navigator === 'undefined') {
    Object.defineProperty(target, 'navigator', {
      value: { gpu },
      writable: true,
      configurable: true,
      enumerable: false,
    });
  } else if (!target.navigator.gpu) {
    Object.defineProperty(target.navigator, 'gpu', {
      value: gpu,
      writable: true,
      configurable: true,
      enumerable: false,
    });
  }
  return gpu;
}

async function requestAdapterFromCreate(create, adapterOptions = undefined, createArgs = null) {
  const gpu = create(createArgs);
  return gpu.requestAdapter(adapterOptions);
}

async function requestDeviceFromRequestAdapter(requestAdapter, options = {}) {
  const createArgs = options?.createArgs ?? null;
  const adapter = await requestAdapter(options?.adapterOptions, createArgs);
  return adapter.requestDevice(options?.deviceDescriptor);
}

function buildProviderInfo({
  moduleName = 'doe-gpu',
  loaded,
  loadError,
  defaultCreateArgs = [],
  doeNative,
  libraryFlavor,
  doeLibraryPath,
  buildMetadataSource,
  buildMetadataPath,
  leanVerifiedBuild,
  proofArtifactSha256,
  buildIdentity = null,
  nativeFastPaths = null,
}) {
  return {
    module: moduleName,
    loaded,
    loadError,
    defaultCreateArgs,
    doeNative,
    libraryFlavor,
    doeLibraryPath,
    buildMetadataSource,
    buildMetadataPath,
    leanVerifiedBuild,
    proofArtifactSha256,
    buildIdentity,
    nativeFastPaths,
  };
}

function libraryFlavor(libraryPath) {
  if (!libraryPath) return 'missing';
  const basename = libraryPath.split(/[\\/]/u).pop();
  // Exact artifact names emitted by dropin, dropin-full and dropin-compute.
  const zigUnix = /^libwebgpu_doe(?:_(?:full|compute))?\.(?:so|dylib)$/u;
  const zigWindows = /^(?:lib)?webgpu_doe(?:_(?:full|compute))?\.dll$/u;
  if (zigUnix.test(basename) || zigWindows.test(basename)) return 'doe-dropin';
  const cUnix = /^libwebgpu_doe_c\.(?:so|dylib)$/u;
  const cWindows = /^(?:lib)?webgpu_doe_c\.dll$/u;
  if (cUnix.test(basename) || cWindows.test(basename)) return 'doe-c';
  if (/lib(webgpu|webgpu_dawn|wgpu_native)\.(so|dylib|dll)/.test(libraryPath)) return 'delegate';
  return 'unknown';
}

export {
  setupGlobalsOnTarget,
  requestAdapterFromCreate,
  requestDeviceFromRequestAdapter,
  buildProviderInfo,
  libraryFlavor,
};
