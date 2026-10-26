import * as path from "node:path";

// Lexical containment for browser assets and diagnostic output paths. Resolve
// first, then compare native path segments so Windows drives and UNC roots stay
// distinct. Filesystem symlink policy belongs to the calling asset owner.
export function pathWithin(pathValue, rootPath, pathApi = path) {
  const child = pathApi.relative(pathApi.resolve(rootPath), pathApi.resolve(pathValue));
  return child === "" || (
    !pathApi.isAbsolute(child)
    && child !== ".."
    && !child.startsWith(`..${pathApi.sep}`)
  );
}
