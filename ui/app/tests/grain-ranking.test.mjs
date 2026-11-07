import test from 'node:test';
import assert from 'node:assert/strict';
import { grainLanes } from '../src/screens/shared/grainLanes.ts';
import { collectSpeciesPages } from '../src/app/liveSpeciesRanking.ts';
const row = (speciesId, count = 1) => ({speciesId, commonName: `Name ${speciesId}`, count});
const event = (id, speciesId) => ({id, speciesId, commonName: `Name ${speciesId}`});
test('live Grain ranks every session species beyond retained ticks and six lanes, with numeric full counts', () => {
  const totals = Array.from({length: 300}, (_, i) => row(String(i).padStart(3, '0'), 300 - i));
  const lanes = grainLanes([event('retained', '299')], [], null, true, totals);
  assert.equal(lanes.length, 300);
  assert.deepEqual(lanes.map(lane => lane.count), totals.map(total => total.count));
  assert.equal(lanes[0].events.length, 0, 'species absent from map sample still has full count row');
  assert.equal(lanes.at(-1).events[0].id, 'retained', 'ticks keep genuine retained events');
});
test('fallback live ties use exact identity order; replay/history keep catalogue order and lane cap', () => {
  const events = [event('a', 'z'), event('b', 'a'), event('c', 'b')];
  assert.deepEqual(grainLanes(events, [], null, true, null).map(lane => lane.speciesId), ['a','b','z']);
  const species = Array.from({length: 9}, (_, i) => ({id: `s${i}`, commonName: `Species ${i}`}));
  const material = species.map((species, i) => event(String(i), species.id));
  assert.deepEqual(grainLanes(material, [...species].reverse(), null, false, [row('other',100)]).map(lane => lane.speciesId), ['s8','s7','s6','s5','s4','s3']);
});
test('full species pagination keeps >250 species and stable count ties in one committed frontier', async () => {
  const rows = Array.from({length: 700}, (_, i) => row(`species-${String(i).padStart(3,'0')}`));
  const calls = [];
  const result = await collectSpeciesPages('session', async cursor => {
    const offset = Number(cursor ?? 0); calls.push(offset);
    return {sessionId:'session',sessionActive:true,revision:'42',species:rows.slice(offset,offset+250),nextCursor:offset+250<rows.length ? String(offset+250) : null};
  });
  assert.deepEqual(calls,[0,250,500]);
  assert.equal(result.length,700);
  assert.equal(result.at(-1).speciesId,'species-699');
});
test('new session, inactive receipt, cancellation and changing frontier cannot expose a partial ranking', async () => {
  for (const page of [{sessionId:'old',sessionActive:true},{sessionId:'session',sessionActive:false}]) {
    assert.equal(await collectSpeciesPages('session', async () => ({revision:'1',species:[row('old')],nextCursor:null,...page})),null);
  }
  let calls = 0;
  assert.equal(await collectSpeciesPages('session',async () => ({sessionId:'session',sessionActive:true,revision:String(++calls),species:[row(String(calls))],nextCursor:calls===1?'1':null})),null);
  assert.equal(await collectSpeciesPages('session',async () => ({sessionId:'session',sessionActive:true,revision:'1',species:[row('old')],nextCursor:null}),()=>true),null);
});
test('malformed repeated pages or identities fail visibly instead of spinning or duplicating rows', async () => {
  await assert.rejects(collectSpeciesPages('session',async () => ({sessionId:'session',sessionActive:true,revision:'1',species:[row('same')],nextCursor:'1'})), /repeated/);
});
