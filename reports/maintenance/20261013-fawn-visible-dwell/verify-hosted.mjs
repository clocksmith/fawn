import { readdir, readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import path from 'node:path';
const root=path.resolve('.fawn-hosting'), origin=process.env.FAWN_VERIFY_ORIGIN;
async function files(dir='') { const rows=[]; for(const entry of await readdir(path.join(root,dir),{withFileTypes:true})) { const name=path.join(dir,entry.name); if(entry.isDirectory())rows.push(...await files(name));else rows.push(name); } return rows; }
const rows=await Promise.all((await files()).sort().map(async name=> { const expected=await readFile(path.join(root,name)); const response=await fetch(origin+'/'+name); assert.equal(response.status,200,name); const got=Buffer.from(await response.arrayBuffer()); assert.ok(expected.equals(got),name+' must match packaged bytes'); return {path:name,bytes:got.length,sha256:createHash('sha256').update(got).digest('hex')}; }));
const receipt={passed:true,checkedAt:new Date().toISOString(),origin,release:JSON.parse(await readFile(path.join(root,'release.json'),'utf8')),files:rows};
await writeFile(process.env.FAWN_VERIFY_OUTPUT,JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify({passed:true,origin,files:rows.length,release:receipt.release}));
