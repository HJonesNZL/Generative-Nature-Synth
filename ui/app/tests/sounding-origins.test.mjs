import test from 'node:test';
import assert from 'node:assert/strict';
import { soundingOrigins, SOUND_FADE_US } from '../src/components/map/soundingOrigins.ts';
const types=['acoustic','tracking','reported'];
const voice=(token,fields={})=>({token,eventId:`event-${token}`,commonName:'Tūī',locationLabel:'Actual receiver',eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-38,longitude:176},onsetUs:'1000000',endUs:null,...fields});
test('only an actual onset lights its exact native origin, without arrival or retained-map input',()=>{
  assert.deepEqual(soundingOrigins([],2_000_000,types).origins,[]);
  assert.equal(soundingOrigins([voice('future',{onsetUs:'3000000'})],2_000_000,types).origins.length,0);
  const result=soundingOrigins([voice('real')],2_000_000,types);
  assert.equal(result.origins.length,1);
  assert.equal(result.origins[0].latitude,-38); assert.equal(result.origins[0].longitude,176);
  assert.deepEqual(result.origins[0].tokens,['real']); assert.equal(result.origins[0].level,1);
});
test('coincident overlapping voices stay fully lit until the last actual end; both fade consistently',()=>{
  const a=voice('a',{endUs:'3000000'}),b=voice('b',{onsetUs:'2000000',endUs:'4000000'});
  const overlapping=soundingOrigins([a,b],3_500_000,types).origins[0];
  assert.equal(overlapping.activeVoices,1);assert.equal(overlapping.level,1);assert.deepEqual(overlapping.tokens,['a','b']);
  const fading=soundingOrigins([a,b],5_000_000,types).origins[0];
  assert.equal(fading.activeVoices,0);assert.equal(fading.level,.5);
  assert.equal(soundingOrigins([a,b],4_000_000+SOUND_FADE_US,types).origins.length,0);
});
test('separate simultaneous origins, early steals and stop follow each token end instead of guessed duration',()=>{
  const voices=[voice('stolen',{endUs:'1100000'}),voice('held',{position:{latitude:40,longitude:-70}})];
  const result=soundingOrigins(voices,2_100_000,types);
  assert.equal(result.origins.length,2);assert.equal(result.origins[0].level,.5);assert.equal(result.origins[1].level,1);
  assert.equal(soundingOrigins(voices,60_000_000,types).origins.length,1,'no inferred end for a voice lacking its native end');
  voices[1].endUs='60000000';
  assert.equal(soundingOrigins(voices,62_000_000,types).origins.length,0,'actual stop end clears the final light after the same fade');
});
test('source filters hide only matching activity and unmappable voices never fabricate coordinates',()=>{
  const reported=voice('reported',{eventKind:'human_observation',locationKind:'reported_location'});
  const missing=voice('missing',{position:null}),invalid=voice('invalid',{position:{latitude:NaN,longitude:0}});
  assert.equal(soundingOrigins([reported],2_000_000,['tracking']).origins.length,0);
  assert.equal(soundingOrigins([reported],2_000_000,['reported']).origins.length,1);
  const result=soundingOrigins([missing,invalid],2_000_000,types);
  assert.equal(result.origins.length,0);assert.equal(result.unmapped,2);
  assert.equal(soundingOrigins([missing],2_000_000,['tracking']).unmapped,0);
});
