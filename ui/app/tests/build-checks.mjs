import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';
for (const [folder, html, demo] of [['dist', 'index.html', false], ['dist-demo', 'demo.html', true]]) {
  const files = await readdir(`${folder}/assets`);
  const maps = await Promise.all(files.filter(file => file.endsWith('.js.map')).map(async file => JSON.parse(await readFile(`${folder}/assets/${file}`, 'utf8'))));
  const sources = maps.flatMap(map => map.sources).join('\n');
  assert.equal(/host\/mockHost\.ts/.test(sources), demo);
  assert.equal(/host\/mockData\.ts/.test(sources), demo);
  const code = (await Promise.all(files.filter(file => file.endsWith('.js')).map(file => readFile(`${folder}/assets/${file}`, 'utf8')))).join('\n');
  assert.equal(code.includes('/__atlas/request'), false, 'Development transport leaked into production');
  assert.match(await readFile(`${folder}/${html}`, 'utf8'), /Content-Security-Policy/);
  for (const license of ['react.txt', 'react-dom.txt', 'scheduler.txt', 'three.txt', 'world-atlas.txt', 'asset-origins.md'])
    assert.ok((await readFile(`${folder}/licenses/${license}`)).length > 50);
  const notices = await readdir(`${folder}/licenses`);
  assert.ok(notices.filter(name => name.startsWith('OFL-')).length >= 3);
  assert.ok(files.some(name => name.startsWith('GlobeView-') && name.endsWith('.js')), 'Globe must remain lazy loaded');
}
console.log('Build checks passed: separate native/demo sources, no development HTTP, CSP, local licences and lazy globe.');
