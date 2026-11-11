// Native installation controls cover local C package tuples independently of publication.
import { cLibraryName } from '../../scripts/c-native-stage.js';

// npm package names; the workspace directory stays the unscoped tuple name.
const PACKAGE_NAMES = Object.freeze({
  'linux-x64': 'doe-gpu-linux-x64',
  'darwin-arm64': 'doe-gpu-darwin-arm64',
  'win32-x64': '@clocksmith/doe-gpu-win32-x64',
});

function nativeInstallPlatform(platform = process.platform, arch = process.arch) {
  const tag = `${platform}-${arch}`;
  const packageName = PACKAGE_NAMES[tag];
  return packageName
    ? { packageName, packageDirectory: `doe-gpu-${tag}`, libraryName: cLibraryName(platform) }
    : null;
}

export { nativeInstallPlatform };
