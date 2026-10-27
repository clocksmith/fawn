// Native installation controls cover local C package tuples independently of publication.
import { cLibraryName } from '../../scripts/c-native-stage.js';

const PACKAGE_NAMES = Object.freeze({
  'linux-x64': 'doe-gpu-linux-x64',
  'darwin-arm64': 'doe-gpu-darwin-arm64',
  'win32-x64': 'doe-gpu-win32-x64',
});

function nativeInstallPlatform(platform = process.platform, arch = process.arch) {
  const packageName = PACKAGE_NAMES[`${platform}-${arch}`];
  return packageName ? { packageName, libraryName: cLibraryName(platform) } : null;
}

export { nativeInstallPlatform };
