import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createServer } from 'node:http';
import { mkdtempSync, readFileSync, writeFileSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WorkspaceService } from '../../src/service.js';
import { loadServiceConfig } from '../../src/config.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { WorkflowStore } from '../../src/client/workflow-store.js';
import { startWorkspaceServer } from '../../src/client/server.js';
import { sha256Hex, canonicalJson } from '../../src/journal.js';

const config=JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json',import.meta.url)));
const root=mkdtempSync(join(tmpdir(),'blitzen-portable-')), posts=[];
const site=createServer((req,res)=>{
  if (req.method!=='GET') posts.push(req.method);
  const account=req.url==='/account-b'?'b':req.url==='/account-a'?'a':'other';
  res.setHeader('content-security-policy',"default-src 'none'; script-src 'unsafe-inline'; form-action 'none'; connect-src 'none'");
  res.setHeader('content-type','text/html');
  res.end('<h1>Account '+account+'</h1><section id="list"><label>Filter<input id="filter" type="text"></label>'
    +'<output id="result">All</output></section><aside>PRIVATE_BODY_SENTINEL</aside>'
    +'<script>filter.oninput=()=>result.textContent="Showing "+filter.value</script>');
});
await new Promise(resolve=>site.listen(0,'127.0.0.1',resolve));
const origin='http://127.0.0.1:'+site.address().port;
const template='lookup';const loc=['textbox','Filter','input','text'];const observation=(service,owner,pageId)=>service.observe({...owner,pageId});
let rulePermitted=true, dropOneAck=false;
async function instance(person) {
  const stateDir=join(root,person);
  const service=await WorkspaceService.open({stateDir:join(stateDir,'service'),
    config:loadServiceConfig(undefined,{browserChannel:process.env.FAWN_WORKSPACE_TEST_BROWSER_CHANNEL??'chrome',headless:true}),
    createAdapter:opts=>{
      const a=createCdpAdapter(opts),execute=a.execute.bind(a);
      a.execute=async (...args)=>{const value=await execute(...args);if(person==='a' && dropOneAck && args[1].kind==='fill'){
        dropOneAck=false;throw Error('Fixture lost acknowledgment after actual browser effect');}return value;};
      return a;
    }});
  const work=new WorkflowStore({directory:join(stateDir,'workflows'),ownerScope:'person-'+person,
    limits:{maxNodes:32,maxEdges:32,maxSteps:8},maxBytes:100000,lockWaitMs:300,lockRetryMs:10});
  const liveUrl=async (owner,pageId)=>(await service.inspect(controller.admin(owner.workspaceId))).pages.find(p=>p.pageId===pageId)?.url;
  const definition={accountScope:'account-'+person,verificationId:'readonly-filter-v1',
    disclosurePolicy:{origin,fields:[{kind:'fill',slot:'search',locator:loc}],intermediateClicks:[]},
    task:'Filter the local read-only list',taskSourceHashes:{fixture:sha256Hex('filter fixture v1')},
    planner:{identity:{model:'controlled-source'},next({observation:obs,inputs}){
      if(person==='b')throw Error('Unexpected recipient model call');
      const field=obs.elements.find(e=>e.label==='Filter');
      return field.value===inputs.search ? {tool:'finish',input:{summary:'Verify the filtered list'}}:
        {tool:'act',input:{kind:'fill',handle:field.handle,slot:'search'}};
    }},
    verifyAccount:async ({owner,pageId})=>new URL(await liveUrl(owner,pageId)).pathname.slice(1),
    verifyReview:async ({owner,pageId,inputs})=>({completed:(await observation(service,owner,pageId)).text.includes('Showing '+inputs.search)}),
    verifyReadOnlyHandoffStep:async ({owner,pageId,step})=>rulePermitted && step.kind==='fill' && step.slot==='search'
      && await liveUrl(owner,pageId)===origin+'/account-'+person};
  const controller=new WorkspaceController({service,stateDir:join(stateDir,'client'),config,
    workflowStore:work,workflowTasks:new Map([[template,definition]])});
  await controller.restore();
  const http=await startWorkspaceServer({controller,config,port:0});
  return {controller,service,http,work,definition,close:async()=>{await http.close();await controller.close();await service.close();}};
}
const cmd=promisify(execFile),cli=fileURLToPath(new URL('../../src/client/handoff-cli.js',import.meta.url));
async function run(app,...args){
  const {stdout}=await cmd(process.execPath,[cli,...args],{env:{...process.env,BLITZEN_LOGIN_URL:app.http.loginUrl},timeout:45000});
  return stdout.trim();
}
const flagFile=join(root,'recipient-inputs.json');writeFileSync(flagFile,JSON.stringify({search:'RECIPIENT_ONLY'}));
let a,b;
try{
  a=await instance('a');b=await instance('b');
  const source=(await a.controller.create({goal:'Private source goal'})).id, target=(await b.controller.create({goal:'Recipient lookup'})).id;
  for(const [app,id,person] of [[a,source,'a'],[b,target,'b']])await app.controller.navigate(id,{pageId:'main',url:origin+'/account-'+person});
  const selected=await a.controller.capture(source,{pageId:'main',regions:['#list']});
  const hidden=await a.controller.capture(source,{pageId:'main'});
  const {done}=await a.controller.runPersonalWorkflow(source,{template,mode:'learn',inputs:{search:'SOURCE_INPUT_SENTINEL'}});
  const learned=await done;assert.equal(learned.completed,true,JSON.stringify(learned));
  const other=(await a.controller.create({goal:'Independent investigation'})).id;
  await assert.rejects(a.controller.exportHandoff(other,{evidenceIds:[],template}),/No verified learned routine/);
  const file=join(root,'handoff.json');
  const id=await run(a,'export',source,file,'--evidence',selected.id,'--routine',template);
  const raw=readFileSync(file,'utf8'),bundle=JSON.parse(raw);
  assert.equal(id,bundle.digest);assert.equal(statSync(file).mode & 0o777,0o600);assert.deepEqual(bundle.evidence.map(r=>r.sha256),[selected.source.sha256]);
  const cache=await a.work.checkout({accountScope:'account-a',template});
  for(const secret of [a.controller.admin(source).adminCredential,cache.dag.key,'PRIVATE_BODY_SENTINEL','SOURCE_INPUT_SENTINEL',hidden.id,selected.id,origin,root,source,'account-a','#filter'])
    assert(!raw.includes(secret),secret);
  const recipientState=()=>b.service.inspect(b.controller.admin(target));
  const before=await recipientState();
  assert.equal(await run(b,'import',target,file),id);assert.equal(await run(b,'import',target,file),id);
  assert.equal((await b.controller.handoff(target)).receivedHandoffs.length,1);
  const inspect=JSON.parse(await run(b,'read',target,id));
  assert.equal(inspect.localTask.template,template);assert.equal(inspect.localTask.steps[0].slot,'search');
  assert.equal((await recipientState()).grants.length,before.grants.length);assert.equal((await recipientState()).actions.length,before.actions.length);
  await b.close();b=await instance('b');
  assert.throws(()=>b.controller.useHandoff(target,{template,inputs:{search:'local'},pageId:'main'}),/imported handoff/);
  assert.equal(JSON.parse(await run(b,'read',target,id)).snapshot.digest,id);
  await b.controller.navigate(target,{pageId:'main',url:origin+'/account-a'});
  const args=['use',target,id,'--template',template,'--page','main','--inputs',flagFile];
  await assert.rejects(run(b,...args),/Website account does not match/);
  await b.controller.navigate(target,{pageId:'main',url:origin+'/account-b'});
  rulePermitted=false;await assert.rejects(run(b,...args),error=>error.code===1 && JSON.parse(error.stdout).completed===false);
  const fills=async()=> (await recipientState()).actions.filter(x=>x.kind==='fill');
  assert.equal((await fills()).length,0);
  rulePermitted=true;
  const used=JSON.parse(await run(b,...args));assert.equal(used.completed,true,JSON.stringify(used));assert.equal(used.modelCalls,0);assert.equal((await fills()).length,1);
  const state=await recipientState();
  const records=readFileSync(join(root,'b','service','journal.jsonl'),'utf8').trim().split('\n').map(line=>JSON.parse(line));
  const granted=records.filter(r=>r.type==='authorization-granted' && r.body.grantId.startsWith('workflow-')).map(r=>r.body);
  assert.equal(granted.length,3);
  for(const {grantId,grant} of granted){
    assert.deepEqual(grant.actionKinds,['fill']);assert.deepEqual(grant.pageIds,['main']);assert.deepEqual(grant.targetOrigins,[origin]);
    assert(records.some(r=>r.type==='authorization-revoked' && r.body.grantId===grantId));
    assert(!state.grants.some(g=>g.grantId===grantId));
  }
  const owner=await a.controller.humanPage(source,'main'), obs=await observation(a.service,owner,'main');
  const element=obs.elements.find(e=>e.label==='Filter');dropOneAck=true;
  const lost=await (await a.service.submitAction({...owner,pageId:'main',authorizedIntent:'human',actionId:'private-write-sentinel',kind:'fill',
    observationVersion:obs.observationVersion,target:{handle:element.handle,selector:element.selector,digest:element.digest},params:{value:'ALREADY_APPLIED'}})).result;
  assert.equal(lost.outcome,'unknown');
  const file2=join(root,'open.json'), id2=await run(a,'export',source,file2,'--no-evidence','--routine',template);
  await run(b,'import',target,file2); const note=JSON.parse(await run(b,'read',target,id2));
  assert.equal(note.snapshot.unresolved.length,1);assert.equal(note.unresolvedAuthority,'source-workspace');
  assert(!readFileSync(file2,'utf8').includes('ALREADY_APPLIED'));
  await assert.rejects(run(b,'use',target,id2,...args.slice(3)),/source workspace/);
  await assert.rejects(b.controller.check(target,{actionId:note.snapshot.unresolved[0].ref}));
  assert.equal((await a.service.action({...owner,actionId:'private-write-sentinel'})).isUncertain,true);
  assert.equal((await fills()).length,1);assert.equal(posts.length,0);
  const poisoned=structuredClone(bundle);poisoned.routine.nodes[0].payload='raw';delete poisoned.digest;poisoned.digest=sha256Hex(canonicalJson(poisoned));
  const bad=join(root,'invalid.json');writeFileSync(bad,JSON.stringify(poisoned));await assert.rejects(run(b,'import',target,bad),/routine edge/);
  console.log(JSON.stringify({completed:true,independentAccounts:2,evidenceSelected:1,importedGrants:0,
    receiverFieldActions:1,wrongAccountFieldActions:0,modelCalls:used.modelCalls,sourceUnknownStillPending:true,siteWrites:posts.length,recipientGrantKinds:granted.at(-1).grant.actionKinds}));
}finally{await a?.close();await b?.close();await new Promise(resolve=>site.close(resolve));rmSync(root,{recursive:true,force:true});}
