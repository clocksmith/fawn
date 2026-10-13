import assert from 'node:assert/strict';
import {createCycleClock,CYCLE_POLICY} from '/Users/xyz/deco/doe/browser/chromium/resources/fawn-cycle-clock.js';
import {writeFile,mkdir} from 'node:fs/promises';
const cap=CYCLE_POLICY.holdS-CYCLE_POLICY.interactionQuietS;
const rows=[];
for(const phase of [0,5,9.5,10,10.5,11,13,15.9]){
 const c=createCycleClock();c.advance(phase);const before=c.state();const held=c.advance(0,true);
 assert.equal(held.phaseS,Math.min(phase,cap));assert.equal(held.blend,before.blend);assert.equal(held.elapsedS,before.elapsedS);assert.equal(held.started,false);
 if(phase>=cap){const wait=c.advance(.99);assert.equal(wait.blend,held.blend);const renewed=c.advance(.01,true);assert.equal(renewed.phaseS,cap);assert.equal(renewed.blend,held.blend);const resume=c.advance(1.5);assert.ok(resume.blend>held.blend);assert.equal(resume.started,!before.transitioning);const end=c.advance(4.5);assert.equal(end.effect,'wave');assert.equal(end.completed,true);}
 rows.push({phase,capped:held.phaseS,blendPreserved:held.blend});
}
const c=createCycleClock();for(let i=0;i<500;i++)c.advance(.1,true);assert.equal(c.state().phaseS,cap);assert.equal(c.state().effect,'flow');assert.ok(c.state().elapsedS>49.9);
c.advance(6);assert.equal(c.state().effect,'wave');
assert.throws(()=>c.advance(-1),/Invalid cycle timestep/);c.reset();assert.equal(c.state().elapsedS,0);
await mkdir('bench/out/fawn-interaction-clock',{recursive:true});await writeFile('bench/out/fawn-interaction-clock/report.json',JSON.stringify({passed:true,policy:CYCLE_POLICY,cases:rows},null,2)+'\n');console.log('Clock cap, uninterrupted blend, renewed quiet period and held input passed');
