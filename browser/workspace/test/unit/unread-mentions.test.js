// A saved conclusion that names code is answered with where that code also occurs in saved sources
// the agent was not shown. The case comes from two graded trial runs: the agent recorded that a
// close "waits with no deadline" from one file, and never opened the part of another saved file
// (past the first 6000 characters) where the deadline is set.
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { WorkspaceService } from '../../src/service.js';
import { loadServiceConfig } from '../../src/config.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { codeTerms, shownRanges, unreadMentions, SHOWN_AT_FIRST_CHARS } from '../../src/client/unread-mentions.js';
import { FakeAdapter, ORIGIN } from '../support/fake-adapter.js';

assert.deepEqual(codeTerms('Browser.close() awaits options.browserProcess.close() in browser.js; see close_or_kill and README.md'),
  ['browserProcess', 'close_or_kill'], 'camelCase and snake_case parts only; file names and plain words are not code terms');
assert.deepEqual(codeTerms('The stall lasts as long as Chrome takes to exit.'), []);

const config = { ...JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url))), maxTurns: 6 };
const root = mkdtempSync(join(tmpdir(), 'fawn-unread-')), repository = join(root, 'repository');
mkdirSync(join(repository, 'lib'), { recursive: true });
const filler = '// unrelated launcher code\n'.repeat(260);
assert.ok(filler.length > SHOWN_AT_FIRST_CHARS);
const LAUNCHER = `let browserProcess = void 0;\n${filler}async function closeOrKill(timeout) { /* race graceful close against a timer, then kill */ }\n`
  + 'browserProcess = { close: () => closeOrKill(DEFAULT_TIMEOUT) };\n';
writeFileSync(join(repository, 'lib/launcher.js'), LAUNCHER);
writeFileSync(join(repository, 'lib/browser.js'), 'export class Browser {\n  async close() { await this.options.browserProcess.close(); await this.disconnected; }\n}\n');
let service, controller;
try {
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config: loadServiceConfig(), createAdapter: () => new FakeAdapter({ receiptsPath: join(root, 'receipts') }) });
  let index = 0, seenResult = null;
  const steps = [
    { tool: 'repository_read', input: { path: 'lib/browser.js' } },
    { tool: 'repository_read', input: { path: 'lib/launcher.js' } },
    context => ({ tool: 'record', input: { kind: 'finding', text: 'Browser.close() awaits options.browserProcess.close() and then waits with no deadline.',
      dependsOn: Object.values(context.artifacts).filter(value => value.source?.path === 'lib/browser.js').map(value => value.id) } }),
    context => { seenResult = context.history.findLast(event => event.event === 'tool-result').output;
      const mention = seenResult.unreadMentions.mentions[0];
      return { tool: 'artifact_read', input: { artifactId: mention.artifactId, expectedRevision: mention.revision, offset: mention.offset, maxChars: 400 } }; },
    context => { const read = context.history.findLast(event => event.event === 'tool-result').output;
      assert.match(read.content, /close: \(\) => closeOrKill\(DEFAULT_TIMEOUT\)/, 'the pointer lands on the code that decides the claim');
      return { tool: 'finish', input: { summary: 'Read the deciding code.' } }; },
  ];
  controller = new WorkspaceController({ service, stateDir: join(root, 'client'), config,
    planner: { identity: { model: 'controlled' }, next: async context => { const step = steps[index++]; return typeof step === 'function' ? step(context) : step; } } });
  await controller.restore();
  const { id } = await controller.create({ goal: 'Explain the close stall', repositoryRoot: repository });
  await controller.navigate(id, { pageId: 'main', url: `${ORIGIN}/form` });
  await controller.delegate(id, { pageIds: ['main'], origins: [ORIGIN], actionKinds: ['navigate'], task: 'Explain why close can stall.' });
  await controller.active.get(id)?.done;
  const project = controller.store.get(id);
  assert.equal(project.run.status, 'completed', project.run.detail);
  const launcher = Object.values(project.artifacts).find(value => value.source?.path === 'lib/launcher.js');
  assert.deepEqual(seenResult.unreadMentions.mentions, [{ term: 'browserProcess', artifactId: launcher.id, revision: 1, path: 'lib/launcher.js',
    offset: LAUNCHER.lastIndexOf('browserProcess = {'), defines: true }], 'the assignment beyond the shown part, not the declaration inside it');
  assert.match(seenResult.unreadMentions.note, /absent, unbounded or never happens/);
  const finding = Object.values(project.artifacts).find(value => value.kind === 'finding');
  assert.equal(Object.hasOwn(finding, 'unreadMentions'), false, 'the pointer is advice in the tool result, not part of the saved record');

  // Once the slice has been read, the same conclusion raises nothing.
  assert.equal(shownRanges(project).get(launcher.id).length, 2);
  assert.equal(unreadMentions(project, finding.text), null);
  // A new revision of the source resets what counts as shown.
  writeFileSync(join(repository, 'lib/launcher.js'), LAUNCHER.replace('DEFAULT_TIMEOUT', 'THIRTY_SECONDS'));
  await controller.checkSources(id);
  assert.equal(unreadMentions(controller.store.get(id), finding.text).mentions[0].revision, 2);
  // Prose with no code names raises nothing, and neither does a short file shown whole.
  assert.equal(unreadMentions(controller.store.get(id), 'Chrome is slow to exit under load.'), null);
  console.log('ok: a conclusion naming code is pointed at its unseen definition in another saved source; read slices and new revisions are tracked');
} finally {
  controller?.store.close(); await service?.close().catch(() => {});
  rmSync(root, { recursive: true, force: true });
}
