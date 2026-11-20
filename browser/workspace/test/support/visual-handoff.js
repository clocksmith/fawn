import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { randomBytes, createHash } from 'node:crypto';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const pageId = 'main', roots = ['#controls', '#cell'];
const sha = value => createHash('sha256').update(value).digest('hex');

// The local application authenticates its own accounts independently of Blitzen.
// Revision and account are validated by the site at commit, including on repeats.
export function visualHandoffFixture() {
  const accounts = ['sender','recipient'].map(name => ({
    name, login: randomBytes(24).toString('hex'), cookie: randomBytes(24).toString('hex'),
  }));
  const attempts = [], effects = [];
  const committed = new Map(accounts.map(({name})=>[name,Promise.withResolvers()]));
  const login = name => '/visual/login/' + accounts.find(person => person.name === name).login;
  function handle(req, res) {
    if (!req.url.startsWith('/visual/')) return false;
    res.setHeader('cache-control','no-store');
    const identity = accounts.find(person => req.headers.cookie?.split('; ').includes('visual=' + person.cookie));
    const signingIn = accounts.find(person => req.url === login(person.name));
    if (signingIn && req.method === 'GET') {
      res.writeHead(303,{location:'/visual/review','set-cookie':'visual='+signingIn.cookie+'; HttpOnly; SameSite=Strict; Path=/; Max-Age=3600'});
      res.end(); return true;
    }
    if (!identity) { res.writeHead(401); res.end(); return true; }
    const writePath = '/visual/write/'+identity.name+'/r1';
    if (req.method === 'POST') {
      attempts.push({ account:identity.name, path:req.url });
      if (req.url !== writePath || effects.includes(identity.name)) { res.writeHead(409); res.end(); return true; }
      effects.push(identity.name);
      committed.get(identity.name).resolve();
      res.writeHead(303,{ location:'/visual/accepted' }); res.end(); return true;
    }
    res.setHeader('content-type','text/html');
    const controls = req.url === '/visual/records' ? (effects.includes(identity.name)?'<p id="verified">Revision 1 recorded</p>':'') :
      req.url === '/visual/review' ? '<form method="post" action="'+writePath+'"><button id="record">Record reviewed selection</button></form>' : 'Accepted';
    res.end('<!doctype html><title>Blitzen ephemeral compositor fixture</title><main id="controls">'+controls+
      '</main><canvas id="cell" width="256" height="128" style="isolation:isolate"></canvas><aside>OUTSIDE_VISUAL_GRANT</aside>'+
      '<script>const c=cell.getContext("2d");c.fillStyle="rgb(113,5,0)";c.fillRect(0,0,256,128);</script>');
    return true;
  }
  async function awaitCommit(name) {
    let timer;
    try {
      await Promise.race([committed.get(name).promise,new Promise((_,reject)=>{
        timer=setTimeout(()=>reject(Error('Site did not record the expected write')),10000);
      })]);
    } finally { clearTimeout(timer); }
  }
  return {login, handle, attempts, effects, awaitCommit, secrets:accounts.flatMap(person => [person.login, person.cookie])};
}

// Reopen the recipient's own state with the ordinary product binary. Its capture
// must subsequently expire even though its durable recipient grant still exists.
export async function crashRecipientWithCapture(stateDir, project, signInUrl) {
  const main = fileURLToPath(new URL('../../src/client/main.js',import.meta.url));
  const child = spawn(process.execPath,[main,'--state-dir',stateDir,'--port','0','--adapter','cdp','--browser-channel','chrome',
    '--compositor-transport','external-texture','--compositor-digest','webgpu'],{stdio:['ignore','pipe','inherit']});
  const exited = once(child,'exit'), lines = createInterface({input:child.stdout});
  let timer;
  try {
    const first = once(lines,'line').then(([line]) => new URL(line.match(/http:\/\/127\.0\.0\.1:\d+\/#[\w-]+/)?.[0]));
    const url = await Promise.race([first,exited.then(()=>{throw Error('Recipient did not start');}),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Recipient startup timed out')),10000);})]);
    async function post(path, body, cookie) {
      const response = await fetch(url.origin+path,{method:'POST',headers:{origin:url.origin,'content-type':'application/json',...(cookie?{cookie}:{})},
        body:JSON.stringify(body),signal:AbortSignal.timeout(20000)});
      const result = await response.json();
      if (!response.ok) throw Error(result.error ?? 'Recipient API request failed');
      return {result,cookie:response.headers.get('set-cookie')?.split(';')[0]};
    }
    const {cookie} = await post('/api/session',{token:url.hash.slice(1)});
    const command = (operation,input)=>post('/api/command',{operation,projectId:project,input},cookie).then(reply=>reply.result);
    await command('navigate',{pageId,url:signInUrl});
    const saved = await command('capture',{pageId,regions:['#controls'],compositorRegion:'#cell'});
    const session = saved.source.compositor.sessionId;
    assert.equal(saved.source.compositor.pixelRepresentation,'video-frame-copy-to-srgb-rgba8');
    assert.ok(child.kill('SIGKILL'));
    assert.deepEqual(await exited,[null,'SIGKILL']);
    const profile='--user-data-dir='+join(stateDir,'service','profiles',project);
    const active=()=>execFileSync('ps',['-axo','stat=,args='],{encoding:'utf8'}).split('\n')
      .some(line=>!line.trimStart().startsWith('Z') && line.split(/\s+/).includes(profile));
    for(let i=0; i<80 && active(); i++) await delay(100);
    assert.equal(active(),false,'Prior recipient browser must exit before reopening its profile');
    return session;
  } finally {
    clearTimeout(timer); lines.close();
    if (child.exitCode === null && child.signalCode === null) { child.kill('SIGKILL'); await exited; }
  }
}

export async function submitSourceControl(app, project, origin, visual) {
  await app.controller.navigate(project,{pageId,url:origin+visual.login('sender')});
  const owner=await app.controller.humanPage(project,pageId,[],roots);
  await app.service.focusPage({...app.controller.admin(project),pageId});
  const obs=await app.service.observe({...owner,pageId,regions:['#controls']});
  const target=obs.elements.find(item=>item.selector==='#record');
  assert.equal(target.formAction,origin+'/visual/write/sender/r1');
  const entry=await app.service.submitAction({...owner,pageId,actionId:'private-write-sentinel',kind:'submit',
    authorizedIntent:'human',observationVersion:obs.observationVersion,target:{handle:target.handle},
    observationScope:{kind:'regions',selectors:['#controls']},effect:{external:true,effectKey:'private-source-effect'},
    completion:{completedWhen:[{kind:'element-present',selector:'#verified'}]}});
  return entry.result;
}

export async function verifyRecipientReview({app,project,origin,visual,retired,sourceGrant,sourceAdmin,imported}) {
  assert.ok(imported.routine);
  const {service,controller,adapter} = app;
  const admin = controller.admin(project), owner = controller.owner(project);
  await assert.rejects(service.inspect({...admin,adminCredential:sourceAdmin}),{code:'unauthenticated'});
  await controller.navigate(project,{pageId,url:origin+visual.login('recipient')});
  // Resolve the transferred reference using only the recipient's live, explicitly
  // selected surface. The transferred record contains neither the frame nor its session.
  const local=await controller.capture(project,{pageId,compositorRegion:'#cell'});
  const matches=imported.evidence.filter(item=>item.sha256===local.source.sha256);
  assert.equal(matches.length,1,'Recipient must reproduce exactly one imported evidence reference');
  assert.notEqual(local.source.compositor.sessionId,sourceGrant);
  await service.revokeAuthorization({...admin,grantId:'human-main'});
  const watch = (session, sinceVersion) => service.observe({...owner,pageId,regions:roots,compositorRegion:'#cell',compositorSession:session,sinceVersion});
  await assert.rejects(watch('start'),{code:'unauthorized-target'});
  const grant = { ...admin, grantId:'recipient-review', ownerId:'human', intents:['review'], pageIds:[pageId],
    targetOrigins:[origin], readRegions:roots };
  await service.grantAuthorization({...grant,actionKinds:['navigate']});
  for (const old of [sourceGrant,retired]) await assert.rejects(watch(old),/expired|changed|ended/);
  await assert.rejects(service.observe({...owner,pageId,regions:['aside']}),{code:'unauthorized-target'});
  function submit(obs, actionId, after) {
    const target = obs.elements.find(item=>item.selector==='#record');
    assert.equal(target.formAction,origin+'/visual/write/recipient/r1','Recipient must bind their own account and current revision');
    return service.submitAction({...owner,pageId,actionId,authorizedIntent:'review',observationVersion:obs.observationVersion,
      kind:'submit',target:{handle:target.handle},observationScope:{kind:'regions',selectors:['#cell']},
      effect:{external:true,effectKey:'recipient-review-r1'},...(after?{resubmissionOf:after}:{}),
      completion:{completedWhen:[{kind:'element-present',selector:'#verified'}]} }).then(entry=>entry.result);
  }
  const readOnly = await watch('start');
  await assert.rejects(submit(readOnly,'read-only'),error=>error.code==='unauthorized-target' && error.details?.neverDispatched?.code===error.code);
  assert.equal(visual.attempts.length,0);
  await service.grantAuthorization({...grant,actionKinds:['submit']});
  await assert.rejects(watch(readOnly.frameEvidence['#cell'].sessionId),/expired|changed|ended/);
  const initial=await watch('start'), session=initial.frameEvidence['#cell'].sessionId;
  assert.equal(new Set([session,sourceGrant,retired,readOnly.frameEvidence['#cell'].sessionId]).size,4);
  const baseline=await watch(session,initial.observationVersion);
  const markup=()=>adapter._evaluate(project,'document.documentElement.outerHTML');
  const before=await markup();
  // This mutation is fixture input. It calls no Blitzen observation or dispatch.
  await adapter._evaluate(project,'(async()=>{const ctx=cell.getContext("2d");ctx.fillStyle="lime";ctx.fillRect(0,0,256,128);await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));return true;})()');
  assert.equal(await markup(),before);
  const stale=await submit(baseline,'visual-stale');
  assert.equal(stale.neverDispatched?.code,'stale-observation');
  assert.equal(visual.attempts.length,0);
  assert.equal(visual.effects.length,0);
  const fresh=await watch(session,baseline.observationVersion), frame=fresh.frameEvidence['#cell'];
  assert.ok(fresh.visualChanges['#cell'].changed.length>0);
  assert.equal(fresh.contentHash,baseline.contentHash);
  assert.equal(frame.grantId,grant.grantId);
  assert.equal(frame.pixelRepresentation,'video-frame-copy-to-srgb-rgba8');
  const unknown=await submit(fresh,'visual-fresh','visual-stale');
  assert.ok(unknown.isUncertain);
  assert.deepEqual(visual.effects,['recipient']);
  await controller.navigate(project,{pageId,url:origin+'/visual/records'});
  // Application-controlled account/revision lookup after the lost reply. Repeating
  // the reconciliation must leave this effect settled.
  const reconcile=()=>service.reconcile({...owner,actionId:'visual-fresh',method:'application-state',
    effectPresentWhen:[{kind:'url-equals',value:origin+'/visual/records'},{kind:'element-present',selector:'#verified'}]});
  assert.equal((await reconcile()).reconciliation.result,'effect-present');
  await assert.rejects(reconcile(),{code:'reconciliation-not-applicable'});
  assert.equal((await service.action({...owner,actionId:'visual-fresh'})).reconciliation.result,'effect-present');
  assert.deepEqual(visual.attempts,[{account:'recipient',path:'/visual/write/recipient/r1'}]);
  assert.equal(adapter._config.browser.channel,'chrome');
  const {cdp}=adapter._entry(project);
  const {product}=await cdp.send('Browser.getVersion');
  const {gpu}=await cdp.send('SystemInfo.getInfo');
  const args=cdp._child.spawnargs;
  return {schema:'blitzen.visual-handoff/v1', stockBrowser:true,product,renderer:gpu.auxAttributes.glRenderer,
    gpuSandboxed:gpu.auxAttributes.sandboxed,browserSandboxDisabled:args.includes('--no-sandbox'),
    automatedCurrentTabCapture:args.includes('--auto-accept-this-tab-capture'),
    handoffTransport:'authenticated loopback CLI export/import and private file', processDeath:'SIGKILL', sourceAdminRejected:true,
    recipientReproducedSelectedEvidence:true,selectedEvidenceSha256:matches[0].sha256,
    sourceCaptureRejected:true,recipientOldCaptureRejected:true,scopeReadOutsideRefused:true,readOnlyWriteRefused:true,
    representation:frame.pixelRepresentation, sourcePixelFormat:frame.sourcePixelFormat, changedTiles:fresh.visualChanges['#cell'].changed,
    domUnchanged:true,markupSha256:sha(before), sameObservationContentHash:true, staleOutcome:stale.neverDispatched.code,
    stalePostAttempts:0,staleEffects:0, freshOutcomeAfterLostAck:'effect-present',
    freshAccount:'recipient',freshPostAttempts:visual.attempts.length, freshEffects:visual.effects.length};
}
