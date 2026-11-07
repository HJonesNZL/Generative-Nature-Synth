import test from 'node:test';
import assert from 'node:assert/strict';
import { currentSessionEvents, liveSessionCatalog } from '../src/app/liveSession.ts';
import { HostClient } from '../src/host/client.ts';
import { EventStore } from '../src/app/eventStore.ts';
globalThis.window = {setTimeout,clearTimeout,requestAnimationFrame:fn=>setTimeout(fn,0),cancelAnimationFrame:clearTimeout};
globalThis.document={hidden:false};
const session={id:'collection-one',storeId:'live-library',baselineRevision:'9007199254740993',observedFromUtc:'2026-11-07T09:00:00.000000Z'};
const event={id:'current',sourceId:'birdweather',speciesId:'current-species',commonName:'Current bird',stationId:'receiver',locationKind:'station_detection',position:{latitude:-41,longitude:174,uncertaintyMetres:null},provenance:'live_source',revisionSequence:'9007199254740994',observedAtUtc:'2026-11-07T09:00:00.000001Z'};
test('strict live scope excludes history, baseline, late old observations, imports and unconfirmed session stores',()=>{
  const records=[event,{...event,id:'baseline',revisionSequence:session.baselineRevision},
    {...event,id:'late-history',observedAtUtc:'2026-11-06T09:00:00Z'}, {...event,id:'import',provenance:'imported'}];
  assert.deepEqual(currentSessionEvents(records,session,session.id),[event]);
  assert.deepEqual(currentSessionEvents([event],{...session,observedFromUtc:'2026-11-07T09:00:00.000002Z'},session.id),[]);
  for(const boundary of [null,{...session,id:'next-session'}]) assert.deepEqual(currentSessionEvents(records,boundary,session.id),[]);
  assert.deepEqual(currentSessionEvents(records,session,null),[]);
});
test('live catalogue derives species from session records and never imports historical names or totals',()=>{
  const metadata={species:[{id:'historic-only',commonName:'Historic seal',scientificName:'Historic',group:'unclassified'},
    {id:event.speciesId,commonName:event.commonName,scientificName:'Current taxon',group:'forest_bird'}],
    stations:[{id:'old-station',name:'Old receiver'},{id:'receiver',name:'Current receiver'}],sources:[{id:'birdweather',name:'BirdWeather'},{id:'archive',name:'Archive'}],limited:true};
  assert.deepEqual(liveSessionCatalog(metadata,[]),{species:[],stations:[],sources:[],limited:false});
  const scoped=liveSessionCatalog(metadata,[event]);
  assert.deepEqual(scoped.species.map(row=>row.id),[event.speciesId]);
  assert.deepEqual(scoped.stations.map(row=>row.id),['receiver']);
  assert.equal(scoped.stations[0].latitude,-41,'Map position comes from the current record, not saved station metadata');
  assert.deepEqual(liveSessionCatalog(metadata,[{...event,position:null}]).stations,[],'Missing current position cannot borrow a historic station location');
  assert.deepEqual(scoped.sources.map(row=>row.id),['birdweather']);
  assert.equal(scoped.limited,false);
  assert.equal(liveSessionCatalog(null,[event]).species[0].commonName,'Current bird');
});
test('clearing the live store retires the session identity, cursor, events and omission counts',()=>{
  const store=new EventStore('isolated');store.liveSessionId=session.id;store.cursor={storeId:session.storeId,nextSequence:'9007199254740994'};
  store.upsert([{event,pulse:false}]);store.setFeed({baselineOmitted:6000,displayOmitted:2});store.clear();
  assert.equal(store.liveSessionId,null);assert.deepEqual(store.all(),[]);assert.equal(store.feed.baselineOmitted,0);assert.equal(store.feed.displayOmitted,0);assert.equal(store.cursor.nextSequence,'0');store.dispose();
});
async function fixture(){
  let payload;
  const client=new HostClient({request:async request=>({ok:true,requestId:request.requestId,result:request.method==='getCapabilities'?{protocolVersion:1,implementation:'native',supportedMethods:['getCapabilities','getLiveChanges','getLiveSpeciesCounts','getLiveSources'],audioAvailable:false,maxEventBatch:500,maxVisibleMarkers:400}:payload})});
  assert.equal((await client.connect()).ok,true);return {client,reply(value){payload=value;}};
}
test('session changes fail closed on malformed identity, data leakage, bounds or provenance',async()=>{
  const {client,reply}=await fixture(),args={sessionId:session.id,storeId:session.storeId,afterSequence:session.baselineRevision,limit:1};
  const valid={sessionId:session.id,sessionActive:true,storeId:session.storeId,nextSequence:event.revisionSequence,removedEventIds:[],hasMore:false,events:[{event,change:'new',liveEligible:true}]};
  reply(valid);assert.equal((await client.call('getLiveChanges',args)).ok,true);
  for(const patch of [{sessionId:'wrong'},{removedEventIds:['']},{removedEventIds:null},{hasMore:1},{sessionActive:'true'},{sessionActive:false},{nextSequence:'-1'},{events:[...valid.events,...valid.events]},
    {events:[{...valid.events[0],event:{...event,provenance:'imported'}}]}]){reply({...valid,...patch});assert.equal((await client.call('getLiveChanges',args)).ok,false,JSON.stringify(patch));}
  reply({...valid,sessionId:'',sessionActive:false,events:[]});assert.equal((await client.call('getLiveChanges',args)).ok,true);
});
test('session species count replies preserve safe aggregate counts and reject malformed or cross-session pages',async()=>{
  const {client,reply}=await fixture();const args={sessionId:session.id,limit:250};
  const valid={sessionId:session.id,sessionActive:true,revision:'999',species:[{speciesId:'bird',commonName:'Current bird',count:45000}],nextCursor:null};
  reply(valid);assert.equal((await client.call('getLiveSpeciesCounts',args)).ok,true);
  reply({...valid,species:[{...valid.species[0],commonName:'n'.repeat(2048)}]});assert.equal((await client.call('getLiveSpeciesCounts',args)).ok,true);
  for(const patch of [{sessionId:'wrong'},{revision:9},{species:[{...valid.species[0],commonName:'n'.repeat(2049)}]},{species:[{...valid.species[0],count:-1}]},{species:[...valid.species,...valid.species]},{nextCursor:''},{sessionActive:false}]){
    reply({...valid,...patch});assert.equal((await client.call('getLiveSpeciesCounts',args)).ok,false,JSON.stringify(patch));
  }
});

test('withdrawn observations leave the store, pending outcome cache and visible pulses',()=>{
  const store=new EventStore('isolated');
  const positioned={...event,position:{latitude:-41,longitude:174,uncertaintyMetres:null}};
  store.upsert([{event:positioned,pulse:true}]);
  assert.equal(store.activePulses(10000).length,1);
  store.remove([event.id]);
  assert.deepEqual(store.all(),[]);assert.deepEqual(store.activePulses(10000),[]);assert.equal(store.get(event.id),null);store.dispose();
});
