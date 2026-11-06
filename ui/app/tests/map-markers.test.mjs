import test from 'node:test';
import assert from 'node:assert/strict';
import { buildMarkers, boundMarkers, newestObservation } from '../src/screens/shared/mapMarkers.ts';
const catalog = { data: { stations: [{ id: 'receiver', name: 'Receiver', latitude: 51, longitude: -1 }] }, speciesName: (id) => id };
const detection = { id: 'detection', stationId: 'receiver', speciesId: 'bird', eventKind: 'acoustic_species_detection', locationKind: 'station_detection', position: { latitude: 51, longitude: -1 } };
const fix = (id) => ({ id, stationId: null, speciesId: 'shark', individualId: 'tag1', eventKind: 'gps_fix', position: { latitude: 40, longitude: -70, uncertaintyMetres: 50 }, observedAtUtc: '2020-01-01T00:00:00Z' });
const view = { events: [], allEvents: [detection, ...Array.from({ length: 45 }, (_, i) => fix(String(i)))] };
test('loaded scope retains old measured fixes without the former forty-fix cutoff', () => {
  const loaded = buildMarkers(view, catalog, 'shark', true);
  assert.equal(loaded.filter((marker) => marker.kind === 'fix').length, 45);
  assert.equal(loaded.find((marker) => marker.key === 'fix:0').uncertaintyMetres, 50);
  assert.match(loaded.find((marker) => marker.key === 'fix:0').label, /GPS fix.*2020/);
  assert.equal(buildMarkers(view, catalog, null, false).filter((marker) => marker.kind === 'fix').length, 0);
});
test('station detections remain receiver aggregates, never animal fixes', () => {
  const loaded = buildMarkers(view, catalog, 'bird', true);
  const station = loaded.find((marker) => marker.kind === 'station');
  assert.equal(station.emphasis, 'highlight');
  assert.match(station.label, /receiver, 1 bird of 1 loaded detections/);
  assert.equal(loaded.some((marker) => marker.key === 'fix:detection'), false);
});
test('display bound preserves selected and highlighted locations and leaves input intact', () => {
  const markers = [{ key: 'quiet', emphasis: 'dim' }, { key: 'highlight', emphasis: 'highlight' }, { key: 'selected', emphasis: 'dim' }];
  assert.deepEqual(boundMarkers(markers, 2, 'selected').map((marker) => marker.key), ['selected', 'highlight']);
  assert.equal(markers[0].key, 'quiet');
});

test('newest observation compares UTC instants, including offsets and sub-millisecond times', () => {
  const later = { observedAtUtc: '2026-01-01T10:00:00.000002Z' };
  const earlier = { observedAtUtc: '2026-01-01T23:00:00.000001+13:00' };
  assert.equal(newestObservation([later, earlier]), later);
  assert.equal(newestObservation([]), null);
});
test('loaded receiver positions remain available when omitted by sampled catalogue', () => {
  const orphan = { ...detection, stationId: 'older-receiver', sourceId: 'fixture' };
  const markers = buildMarkers({ events: [], allEvents: [orphan] }, { ...catalog, data: { stations: [] } }, null, true);
  assert.equal(markers.length, 1);
  assert.equal(markers[0].key, 'station:older-receiver');
  assert.equal(markers[0].latitude, 51);
});

test('individual fix station metadata never counts as a receiver detection', () => {
  const gps = { ...fix('with-site'), locationKind: 'individual_fix', stationId: 'receiver' };
  const markers = buildMarkers({ events: [], allEvents: [gps] }, catalog, null, true);
  assert.match(markers.find(marker => marker.kind === 'station').label, /0 loaded detections/);
  assert.equal(markers.filter(marker => marker.kind === 'fix').length, 1);
  const withoutCatalogue = buildMarkers({ events: [], allEvents: [gps] }, { ...catalog, data: { stations: [] } }, null, true);
  assert.equal(withoutCatalogue.filter(marker => marker.kind === 'station').length, 0);
});
