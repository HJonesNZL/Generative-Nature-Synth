import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { mapProjection, loupeViewport, dragLoupePan, clampLoupePan, LOUPE_PAN_LIMITS } from '../src/components/map/loupeGeometry.ts';

const bounds = { west: 165.6, east: 179.2, south: -47.6, north: -34 };
const projection = mapProjection(bounds);
const coast = JSON.parse(readFileSync(new URL('../src/assets/geo/nz-50m.json', import.meta.url))).rings;
const inside = ([x, y], view, margin = 0) => Math.hypot(x - view.x - view.width / 2, y - view.y - view.height / 2) <= view.width / 2 - margin;

test('fixed geographic projection preserves longitude/latitude proportions and real positions during pans', () => {
  assert.deepEqual(projection.project(bounds.west, bounds.north), [0, 0]);
  assert.equal(projection.project(bounds.east, bounds.north)[0], 1000);
  const north = projection.project(176, -38), south = projection.project(169, -45);
  const view = loupeViewport(bounds, { x: 120, y: -500 });
  assert.equal(view.width, 1000);
  const original = loupeViewport(bounds, { x: 0, y: 0 });
  assert.equal(view.x - original.x, -120);
  assert.equal(view.y - original.y, 500);
  assert.ok(Math.abs((north[0] - south[0]) / (south[1] - north[1]) - Math.cos(-40.8 * Math.PI / 180)) < 1e-12);
  assert.deepEqual(projection.project(176, -38), north);
});

test('pan uses CSS pixel scale consistently at supported loupe widths and clamps both geographic extremes', () => {
  for (const width of [180, 200, 250]) {
    assert.deepEqual(dragLoupePan({ x: 0, y: 0 }, width / 10, -width / 2, width, 1000), { x: 100, y: -500 });
  }
  assert.deepEqual(clampLoupePan({ x: 1e8, y: -1e8 }), { x: 350, y: -850 });
  assert.deepEqual(clampLoupePan({ x: -1e8, y: 1e8 }), { x: -350, y: 300 });
  assert.deepEqual(clampLoupePan({ x: NaN, y: 3 }), { x: 0, y: 0 });
  assert.deepEqual(dragLoupePan({ x: 12, y: 34 }, 80, 80, 0, 1000), { x: 12, y: 34 });
});

test('North, South and Stewart islands can reach the centre without changing enlarged map scale', () => {
  for (const [lon, lat] of [[174.7, -36.8], [170.5, -43.5], [167.9, -47]]) {
    const [x, y] = projection.project(lon, lat);
    const initial = loupeViewport(bounds, { x: 0, y: 0 });
    const pan = clampLoupePan({ x: initial.x + 500 - x, y: initial.y + 500 - y });
    const view = loupeViewport(bounds, pan);
    assert.ok(inside([x, y], view, 400), `Island at ${lon}, ${lat} reaches the central detail area`);
    assert.equal(view.width, initial.width);
  }
});

test('even extreme bounded views retain real NZ coastline, and reset returns the original view', () => {
  for (const x of [LOUPE_PAN_LIMITS.minX, 0, LOUPE_PAN_LIMITS.maxX]) {
    for (const y of [LOUPE_PAN_LIMITS.minY, 0, LOUPE_PAN_LIMITS.maxY]) {
      const view = loupeViewport(bounds, { x, y });
      assert.ok(coast.flat().some(([lon, lat]) => inside(projection.project(lon, lat), view, 30)), `Recoverable coastline at ${x}, ${y}`);
    }
  }
  assert.deepEqual(loupeViewport(bounds, { x: 0, y: 0 }), loupeViewport(bounds, clampLoupePan({ x: 0, y: 0 })));
});
