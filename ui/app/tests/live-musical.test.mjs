import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';
globalThis.window = {setTimeout, clearTimeout};
const composition = {enabled:true,tempoBpm:90,tonicPitchClass:0,scale:'majorPentatonic',density:1,variation:.25,seed:1,quantize:true};
const spatial = {enabled:false,amount:.25,regions:[]};
const live = {enabled:false,status:'off',received:0,admitted:0,dropped:0,skippedRevisions:0,error:'',mode:'musical',density:3,grouped:0,composition,spatial};
async function fixture(methods=['getLiveState','setLiveSettings','getLiveOutcome']) {
  const state={reply:live,calls:[]};
  const client=new HostClient({request:async request => {
    state.calls.push(request);
    return {ok:true,requestId:request.requestId,result:request.method==='getCapabilities' ? {protocolVersion:1,implementation:'native',supportedMethods:['getCapabilities',...methods],audioAvailable:true,maxEventBatch:500,maxVisibleMarkers:400} : state.reply};
  }});
  assert.equal((await client.connect()).ok,true);
  return {client,state};
}
test('live settings accept patch mutations while listening remains explicitly off', async()=>{
  const {client,state}=await fixture();
  assert.deepEqual(await client.call('setLiveSettings',{density:8}),{ok:true,value:live});
  assert.deepEqual(state.calls.at(-1).args,{density:8});
  assert.equal(state.calls.some(c=>c.method==='setLiveEnabled'),false);
});
test('old hosts accept legacy live state and block new methods before sending',async()=>{
  const {client,state}=await fixture(['getLiveState']);
  const {mode,density,grouped,composition,spatial,...legacy}=live; state.reply=legacy;
  assert.equal((await client.call('getLiveState',{})).ok,true);
  assert.equal((await client.call('setLiveSettings',{mode:'musical'})).error.code,'unsupported');
  assert.equal((await client.call('getLiveOutcome',{eventId:'observation-1'})).error.code,'unsupported');
  assert.equal(state.calls.length,2);
});
test('malformed live settings fail visibly instead of presenting fabricated success',async()=>{
  const {client,state}=await fixture();
  for(const patch of [{mode:'replay'},{density:0},{density:9},{density:1.5},{grouped:-1},{composition:{...composition,scale:'invented'}},{spatial:{...spatial,amount:2}}]) {
    state.reply={...live,...patch}; assert.equal((await client.call('getLiveState',{})).ok,false,JSON.stringify(patch));
  }
  state.reply={enabled:false,status:'off',received:0,admitted:0,dropped:0,skippedRevisions:0,error:''};
  assert.equal((await client.call('setLiveSettings',{mode:'musical'})).ok,false);
});
test('individual observation outcome preserves grouped/skipped/queued and unknown history',async()=>{
  const {client,state}=await fixture();
  for(const status of ['queued','sounded','grouped','skipped']) {
    state.reply={outcome:{eventId:'observation-1',speciesId:'bird',region:'Pacific',status,representativeId:status==='grouped'?'observation-0':null}};
    assert.deepEqual(await client.call('getLiveOutcome',{eventId:'observation-1'}),{ok:true,value:state.reply});
  }
  state.reply={outcome:null}; assert.equal((await client.call('getLiveOutcome',{eventId:'older-event'})).ok,true);
  state.reply={outcome:{eventId:'observation-1',speciesId:'',region:'Unassigned',status:'skipped',representativeId:null}};
  assert.equal((await client.call('getLiveOutcome',{eventId:'observation-1'})).ok,true);
  for(const outcome of [{},null,{eventId:'x',speciesId:'x',region:'Ocean',status:'invented',representativeId:null}]) {
    state.reply=outcome===null?{}:{outcome}; assert.equal((await client.call('getLiveOutcome',{eventId:'x'})).ok,false);
  }
});

test('live outcomes retain exact species voice and full backend identity bounds',async()=>{
  const {client,state}=await fixture();
  const outcome={eventId:'e'.repeat(2048),speciesId:'s'.repeat(512),region:'Pacific',status:'sounded',representativeId:'r'.repeat(2048)};
  for(const voiceCharacter of [undefined,null,'struck','whistle','reed','bell','air','pulse']) {
    state.reply={outcome:{...outcome,voiceCharacter}};
    assert.equal((await client.call('getLiveOutcome',{eventId:outcome.eventId})).ok,true,String(voiceCharacter));
  }
  for(const patch of [{eventId:'e'.repeat(2049)},{speciesId:'s'.repeat(513)},{representativeId:'r'.repeat(2049)},{voiceCharacter:'unknown'},{voiceCharacter:3}]) {
    state.reply={outcome:{...outcome,...patch}};
    assert.equal((await client.call('getLiveOutcome',{eventId:outcome.eventId})).ok,false,JSON.stringify(patch));
  }
});
