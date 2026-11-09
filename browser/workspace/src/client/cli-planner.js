// A planner-only CLI port: no file, shell, browser or MCP tools, and no project context discovery.
import { spawn as spawnProcess } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { PLANNER_SYSTEM, PLANNER_TOOLS } from './planner.js';

export function createCliPlanner({ executable, model, directory, config, spawn = spawnProcess }) {
  mkdirSync(directory, { recursive: true, mode: 0o700 });
  const schema = { type: 'object', additionalProperties: false, required: ['tool', 'input'], properties: {
    tool: { type: 'string', enum: Object.keys(PLANNER_TOOLS) }, input: { type: 'object' }
  } };
  return {
    identity: { provider: 'claude-cli', model: model ?? 'CLI configured default; actual model recorded per turn' },
    next(context, { signal }) {
      signal.throwIfAborted();
      const args = ['--print', '--output-format', 'json', '--json-schema', JSON.stringify(schema),
        '--tools', '', '--strict-mcp-config', '--mcp-config', '{"mcpServers":{}}', '--safe-mode', '--restricted',
        '--setting-sources', '', '--no-session-persistence', '--max-budget-usd', String(config.cliMaxBudgetUsd),
        '--system-prompt', `${PLANNER_SYSTEM}\nTool contracts: ${JSON.stringify(PLANNER_TOOLS)}`,
        ...(model ? ['--model', model] : [])];
      return new Promise((resolve, reject) => {
        const child = spawn(executable, args, { cwd: directory, stdio: ['pipe', 'pipe', 'pipe'], shell: false });
        let output = '', bytes = 0, failure = null, killTimer;
        const stop = error => { if (failure) return; failure = error; child.kill('SIGTERM'); killTimer = setTimeout(() => child.kill('SIGKILL'), config.cliTerminationGraceMs); };
        const abort = () => stop(signal.reason ?? Error('Planner cancelled'));
        signal.addEventListener('abort', abort, { once: true });
        const timeout = setTimeout(() => stop(Error('Planner deadline exceeded')), config.modelTimeoutMs);
        child.on('error', error => { failure ??= Error(`Planner could not start: ${error.code ?? 'process error'}`); });
        child.stdout.on('data', chunk => { bytes += chunk.length; if (bytes > config.maxBodyBytes) stop(Error('Planner output exceeds its byte budget')); else output += chunk; });
        // Drain diagnostics without exposing credentials or unbounded provider error bodies to the UI.
        child.stderr.resume();
        child.stdin.on('error', () => {});
        child.once('close', code => {
          clearTimeout(timeout); clearTimeout(killTimer); signal.removeEventListener('abort', abort);
          if (failure) return reject(failure);
          if (code !== 0) return reject(Error(`Planner exited with status ${code}`));
          try {
            const response = JSON.parse(output), proposal = response.structured_output;
            if (response.is_error || !proposal || !Object.hasOwn(PLANNER_TOOLS, proposal.tool)
              || !proposal.input || typeof proposal.input !== 'object' || Array.isArray(proposal.input)) throw Error('Planner returned no valid structured tool proposal');
            resolve({ ...proposal, usage: { ...response.usage, costUsd: response.total_cost_usd ?? null,
              models: Object.keys(response.modelUsage ?? {}) } });
          } catch (error) { reject(error); }
        });
        if (signal.aborted) abort();
        child.stdin.end(JSON.stringify(context));
      });
    }
  };
}
