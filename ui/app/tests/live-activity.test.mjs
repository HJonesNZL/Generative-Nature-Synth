import test from 'node:test';
import assert from 'node:assert/strict';
import {LiveActivityCache,LiveActivityClock,activityIsSounding,activityLocation,activityVoiceName,activityAccuracy} from '../src/app/liveActivity.ts';
import {ribbonInterval,ribbonLanes} from '../src/screens/shared/ribbonLayout.ts';
import {HostClient} from '../src/host/client.ts';
globalThis.window={setTimeout,clearTimeout};
const voice={token:'prep-one:1',eventId:'birdweather:detection:1',speciesId:'bird',commonName:'Tūī',voiceName:'Resonant pluck',voiceType:'pluck',position:{latitude:-41.28,longitude:174.77,uncertaintyMetres:1200},stationId:'birdweather:station:123',eventKind:'acoustic_species_detection',locationKind:'station_detection',locationLabel:'Receiver · birdweather:station:123',region:null,onsetUs:'5000000',endUs:null};
const page=(patch={})=>({sessionId:'collect-1',sessionActive:true,clockUs:'10000000',voices:[],dropped:0,nextSequence:'0',hasMore:false,reset:false,...patch});
test('incremental playback cache preserves simultaneous tokens, native timbre and exact end updates',()=>{
  const cache=new LiveActivityCache();assert.deepEqual(cache.snapshot(page()).voices,[]);
  cache.apply(page({voices:[voice,{...voice,token:'prep-two:1',voiceType:'bell',voiceName:'Bell'}],nextSequence:'2',reset:true}));
  const before=cache.snapshot(page());assert.equal(before.voices.length,2);
  cache.apply(page({voices:[{...voice,endUs:'8000000'}],nextSequence:'3'}));
  const after=cache.snapshot(page());assert.equal(after.voices.length,2);assert.equal(after.voices[0].endUs,'8000000');assert.equal(after.voices[1].voiceName,'Bell');assert.equal(before.voices[0].endUs,null,'Published snapshots stay immutable');
  assert.equal(activityIsSounding(after.voices[0],10000000),false);assert.equal(activityIsSounding(after.voices[1],10000000),true);
});
test('history expires by actual end time while an ongoing old note remains; reset retires tokens',()=>{
  const cache=new LiveActivityCache();cache.apply(page({voices:[{...voice,endUs:'8000000'},{...voice,token:'ongoing'}],nextSequence:'2'}));
  cache.apply(page({clockUs:'39000001',nextSequence:'2'}));assert.deepEqual(cache.snapshot(page()).voices.map(row=>row.token),['ongoing']);
  cache.apply(page({reset:true,nextSequence:'3'}));assert.deepEqual(cache.snapshot(page()).voices,[]);
});
test('native-shaped location labels do not duplicate meaning, invent a city or expose canonical IDs as geography',()=>{
  assert.equal(activityLocation({...voice,locationLabel:'Acoustic receiver location',position:{latitude:-41,longitude:174,uncertaintyMetres:null}}),'Receiver · 41° S, 174° E');
  assert.equal(activityLocation(voice),'Receiver · 41° S, 175° E · ±1.2 km');
  assert.equal(activityLocation({...voice,region:'Wellington region'}),'Receiver · Wellington region · ±1.2 km');
  assert.equal(activityLocation({...voice,locationLabel:'Receiver · Receiver · Wellington region'}),'Receiver · Wellington region · ±1.2 km');
  assert.equal(activityLocation({...voice,locationLabel:'Location withheld',region:'Wellington region'}),'Receiver · location withheld');
  assert.equal(activityLocation({...voice,eventKind:'human_observation',locationKind:'reported_location',locationLabel:'Reported location',position:null,region:null}),'Reported location · location unavailable');
  assert.equal(activityLocation({...voice,eventKind:'gps_fix',locationKind:'measured_location',locationLabel:'',region:null,stationId:null}),'Measured fix · 41° S, 175° E · ±1.2 km');
  assert.equal(activityAccuracy({...voice,position:{latitude:0,longitude:0,uncertaintyMetres:null}}),'accuracy not supplied');
  assert.equal(activityLocation({...voice,eventKind:'gps_fix',locationKind:'animal_location',locationLabel:'Reported individual fix (approximate)'}),'Measured fix · 41° S, 175° E · approximate · ±1.2 km');
  assert.equal(activityVoiceName(voice),'Resonant pluck');
});
test('ribbon uses elapsed native times and retains overlapping notes instead of tempo bins or fabricated envelopes',()=>{
  const ended={...voice,endUs:'8000000'};const interval=ribbonInterval(ended,10000000);
  assert.ok(Math.abs(interval.start-83.333333333)<.0001);assert.ok(Math.abs(interval.width-10)<.0001);assert.equal(interval.active,false);
  const lanes=ribbonLanes([voice,{...voice,token:'other'}],10000000);assert.equal(lanes.length,1);assert.equal(lanes[0].voices.length,2);
  assert.deepEqual(ribbonLanes([{...voice,endUs:'8000000'}],40000001),[]);
});
test('shared presentation clock samples native time without generating activity',()=>{
  const clock=new LiveActivityClock();assert.equal(clock.sampleNowUs(),0);clock.anchor('0');clock.tick();assert.ok(clock.getSnapshot()>=0);clock.anchor('10000000');clock.tick();assert.ok(clock.getSnapshot()>=10000000);clock.reset();assert.equal(clock.getSnapshot(),0);
});
async function fixture(){let reply;const client=new HostClient({request:async request=>({ok:true,requestId:request.requestId,result:request.method==='getCapabilities'?{protocolVersion:1,implementation:'native',supportedMethods:['getCapabilities','getLiveActivity'],audioAvailable:false,maxEventBatch:500,maxVisibleMarkers:400}:reply})});assert.equal((await client.connect()).ok,true);return{client,set:value=>reply=value};}
test('activity boundary accepts native tokens and rejects malformed clocks, origins, ends, timbres and non-progressing pages',async()=>{
  const {client,set}=await fixture();const args={sessionId:'collect-1',afterSequence:'0',limit:250};set(page({voices:[voice],nextSequence:'1'}));assert.equal((await client.call('getLiveActivity',args)).ok,true);
  for(const patch of [{sessionId:'different'},{clockUs:'-1'},{nextSequence:3},{hasMore:true},{dropped:-1},{sessionActive:false,voices:[voice]},{voices:[{...voice,voiceType:'invented'}]},{voices:[{...voice,endUs:'1'}]},{voices:[{...voice,position:{latitude:91,longitude:0}}]},{voices:[{...voice,position:{latitude:0,longitude:0,uncertaintyMetres:-1}}]}]){set(page(patch));assert.equal((await client.call('getLiveActivity',args)).ok,false,JSON.stringify(patch));}
});
