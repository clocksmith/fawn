import { open, writeFile } from 'node:fs/promises';
import { validateHandoff } from './portable-handoff.js';

const usage = [
  'npm run handoff -- export PROJECT NEW.json (--evidence ID ... | --no-evidence) [--routine TEMPLATE]',
  'npm run handoff -- import PROJECT FILE.json',
  'npm run handoff -- read PROJECT DIGEST',
  'npm run handoff -- use PROJECT DIGEST --template TEMPLATE --page PAGE --inputs RECIPIENT.json',
  'Set BLITZEN_LOGIN_URL to the login link printed by the local workspace you are using.',
].join('\n');

async function main(args) {
  if (args.includes('--help') || args.length === 0) { console.log(usage); return; }
  const [verb, projectId, target, ...flags] = args;
  if (!['export','import','read','use'].includes(verb) || !projectId || !target || (!['use','export'].includes(verb) && flags.length)) throw Error(usage);
  let operation, input;
  if (verb === 'export') {
    operation = 'exportHandoff';
    const evidenceIds = [];
    let template = null, none = false;
    for (let i=0; i<flags.length; i++) {
      const flag = flags[i];
      if (flag === '--no-evidence' && !none) { none = true; continue; }
      if (!flags[i+1] || flags[i+1].startsWith('--')) throw Error(usage);
      if (flag === '--evidence') evidenceIds.push(flags[++i]);
      else if (flag === '--routine' && template === null) template = flags[++i];
      else throw Error(usage);
    }
    if (none === Boolean(evidenceIds.length)) throw Error('Choose evidence IDs or explicitly pass --no-evidence');
    input = {evidenceIds,template};
  } else if (verb === 'import') {
    operation = 'importHandoff';
    const file = await open(target, 'r');
    try {
      const stat = await file.stat();
      if (!stat.isFile() || stat.size > 1024*1024) throw Error('Choose a handoff file no larger than 1 MiB');
      input = validateHandoff(JSON.parse(await file.readFile('utf8')));
    } finally { await file.close(); }
  } else if (verb === 'use') {
    if (flags.length !== 6 || flags[0] !== '--template' || flags[2] !== '--page' || flags[4] !== '--inputs'
      || !/^[a-f0-9]{64}$/.test(target)) throw Error(usage);
    const file = await open(flags[5],'r');
    try {
      const st = await file.stat();
      if (!st.isFile() || st.size > 1024*1024) throw Error('Choose a bounded recipient input JSON file');
      input = { digest:target, template:flags[1], pageId:flags[3], inputs:JSON.parse(await file.readFile('utf8')) };
    } finally { await file.close(); }
    operation = 'useHandoff';
  } else {
    operation = 'readHandoff';
    if (!/^[a-f0-9]{64}$/.test(target)) throw Error('Expected an imported handoff digest');
    input = {digest:target};
  }
  if (!process.env.BLITZEN_LOGIN_URL) throw Error('Set BLITZEN_LOGIN_URL to the printed local login link');
  const url = new URL(process.env.BLITZEN_LOGIN_URL);
  if (url.protocol !== 'http:' || !['127.0.0.1','[::1]','localhost'].includes(url.hostname) || url.username || url.password
    || !/^[#][\w-]+$/.test(url.hash) || url.pathname !== '/' || url.search) throw Error('Use the printed loopback login link');
  const post = (path,body,cookie) => fetch(url.origin+path,{method:'POST',redirect:'error',signal:AbortSignal.timeout(verb === 'use' ? 180000 : 15000),
    headers:{origin:url.origin,'content-type':'application/json',...(cookie?{cookie}:{})},body:JSON.stringify(body)});
  const login = await post('/api/session',{token:url.hash.slice(1)});
  if (!login.ok) throw Error('Local workspace login failed');
  const cookie = login.headers.get('set-cookie')?.split(';')[0]; await login.body?.cancel();
  if (!cookie) throw Error('Local workspace did not issue a session');
  const response = await post('/api/command',{operation,projectId,input},cookie);
  const value = await response.json();
  if (!response.ok) throw Error(value.error || 'Local handoff command failed');
  if (verb === 'export') {
    validateHandoff(value);
    await writeFile(target,JSON.stringify(value,null,2)+'\n',{mode:0o600,flag:'wx'});
    console.log(value.digest);
  } else {
    console.log(verb === 'import' ? value.digest : JSON.stringify(value, null, 2));
    if (verb === 'use' && !value.completed) process.exitCode = 1;
  }
}
try { await main(process.argv.slice(2)); } catch(error) { console.error(error.message); process.exitCode=1; }
