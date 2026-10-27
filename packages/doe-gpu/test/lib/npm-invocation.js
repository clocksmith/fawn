// Invoke npm's JavaScript CLI directly: Windows .cmd files are not executables.
import { existsSync, realpathSync } from 'node:fs';
import { basename, delimiter, dirname, resolve } from 'node:path';

function npmInvocation({ executable = process.execPath, environment = process.env } = {}) {
  const candidates = [];
  if (environment.npm_execpath && basename(environment.npm_execpath) === 'npm-cli.js') {
    candidates.push(environment.npm_execpath);
  }
  const directories = [dirname(executable), ...(environment.PATH ?? environment.Path ?? '').split(delimiter)];
  for (const directory of directories.filter(Boolean)) {
    candidates.push(resolve(directory, 'node_modules/npm/bin/npm-cli.js'));
    const npm = resolve(directory, process.platform === 'win32' ? 'npm.cmd' : 'npm');
    if (existsSync(npm)) {
      const target = realpathSync(npm);
      if (basename(target) === 'npm-cli.js') candidates.push(target);
    }
  }
  const cli = candidates.find((file) => existsSync(file));
  if (!cli) throw new Error('Cannot locate npm-cli.js beside Node, on PATH, or via npm_execpath');
  return { executable, prefixArgs: [realpathSync(cli)] };
}

export { npmInvocation };
