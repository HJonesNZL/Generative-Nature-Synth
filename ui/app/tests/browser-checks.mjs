// Requires the disposable tests/ui_fixture.py library served by npm run dev.
// Supply PLAYWRIGHT_MODULE when using an externally installed Playwright.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const browser = await chromium.launch({ headless: true, channel: process.env.ATLAS_BROWSER || 'chrome', args: ['--enable-unsafe-swiftshader'] });
await mkdir('test-results', { recursive: true });
const errors = [], external = [], checks = [];
const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, timezoneId: 'Pacific/Auckland' });
page.on('response', response => { if (response.status() >= 400) console.log('HTTP', response.status(), response.request().postData()?.slice(0,180)); });
page.on('pageerror', error => errors.push(error.message));
page.on('request', request => { if (!request.url().startsWith(origin) && /^https?:/.test(request.url())) external.push(request.url()); });
const visible = text => page.getByText(text, { exact: true }).waitFor();
const disabled = async locator => assert.equal(await locator.isDisabled(), true);
const screen = name => page.getByRole('button', { name, exact: true }).click();
try {
  await page.goto(origin);
  await page.getByLabel('Project', { exact: true }).click();
  await page.locator('.project-tools__help summary').click();
  await visible('Audio output is unavailable in this host. Available data and export controls still work.');
  await page.keyboard.press('Escape');
  await visible('Source status unavailable');
  await page.locator('.map-stage canvas').waitFor();
  assert.equal(await page.getByText('DEMO', { exact: true }).count(), 0);
  await page.getByRole('button', { name: 'Map details and coverage', exact: true }).click();
  await page.getByRole('radio', { name: 'All loaded records', exact: true }).waitFor();
  await page.keyboard.press('Escape');
  await page.getByRole('button', { name: 'All available species', exact: true }).click();
  await page.locator('.species-list li').first().waitFor();
  await page.getByRole('searchbox', { name: 'Find species', exact: true }).fill('no-such-species-91753');
  assert.equal(await page.locator('.species-list li').count(), 0);
  await page.getByRole('searchbox', { name: 'Find species', exact: true }).fill('');
  assert.ok(await page.locator('.species-list li').count() > 0);
  await page.getByRole('radio', { name: '2D map', exact: true }).click();
  await page.getByRole('group', { name: 'World map', exact: true }).waitFor();
  await page.locator('.map-locations summary').focus();
  await page.keyboard.press('Enter');
  assert.ok(await page.locator('.map-locations button').count() > 0);
  await page.locator('.map-locations button').first().focus();
  await page.keyboard.press('Enter');
  await page.screenshot({ path: 'test-results/native-world-overview.png' });
  await page.locator('.map-locations summary').click();
  await page.getByRole('radio', { name: 'Globe', exact: true }).click();
  checks.push('All loaded scope, species search/reset, world fallback and keyboard location list');
  await disabled(page.getByRole('radio', { name: 'History', exact: true }));
  await disabled(page.getByLabel('Master level', { exact: true }));
  await page.getByRole('radio', { name: 'Replay', exact: true }).click();
  await page.getByRole('button', { name: /Fixture • 2,005 records/ }).click();
  await visible('Timeline verified. Audio playback is not implemented yet.');
  await page.getByRole('button', { name: 'Map details and coverage', exact: true }).click();
  await visible('5 records omitted from this display; stored material is unchanged.');
  await page.keyboard.press('Escape');
  assert.match(await page.locator('.grain__axis').innerText(), /13:00[\s\S]*14:00/);
  await disabled(page.getByRole('button', { name: 'Play', exact: true }));
  await page.screenshot({ path: 'test-results/native-atlas.png' });
  console.log('CHECK', checks.length + 1); checks.push('Native capabilities, verified historical bounds and explicit 2000-record display limit');
  await screen('Sound Studio');
  await visible('Voice library unavailable');
  await visible('Region effects unavailable');
  assert.equal(await page.getByRole('button', { name: 'Export saved WAV', exact: true }).count(), 0);
  await page.screenshot({ path: 'test-results/native-studio.png' });
  for (let i = 0; i < 4; i++) {
    await screen('Atlas'); await page.locator('.map-stage canvas').waitFor();
    assert.equal(await page.locator('.map-stage canvas').count(), 1);
    await screen('Sound Studio');
    assert.equal(await page.locator('.map-stage canvas').count(), 0);
  }
  await screen('Atlas');
  await page.getByRole('button', { name: /Fixture • empty historical hour/ }).click();
  await visible('Timeline verified. Audio playback is not implemented yet.');
  await page.getByText('No records in this window · coverage may be incomplete.', { exact: true }).waitFor();
  assert.match(await page.locator('.grain__axis').innerText(), /13:00[\s\S]*14:00/);
  assert.match(await page.locator('.header-clock').innerText(), /0:01:00/);
  assert.equal(await page.getByText('Display limited:', { exact: false }).count(), 0);
  console.log('CHECK', checks.length + 1); checks.push('Empty historical hour preserved; Studio capability gating; repeated globe mount/disposal');
  await page.locator('.map-stage canvas').waitFor();
  await page.locator('.map-stage canvas').evaluate(canvas => canvas.getContext('webgl2').getExtension('WEBGL_lose_context').loseContext());
  await visible('3D graphics stopped');
  assert.equal(await page.locator('.map-stage canvas').count(), 0);
  console.log('CHECK', checks.length + 1); checks.push('WebGL context loss switches to 2D without losing data');
  await page.goto(origin + '/demo.html');
  await visible('DEMO');
  await page.locator('.map-stage canvas').waitFor();
  await page.getByRole('radio', { name: '2D map', exact: true }).focus();
  await page.keyboard.press('Space');
  assert.equal(await page.locator('.map-stage canvas').count(), 0);
  for (const [width, height] of [[1280, 800], [1440, 900], [1920, 1080]]) {
    await page.setViewportSize({ width, height });
    for (const name of ['Atlas', 'Sound Studio']) {
      await screen(name);
      if (name === 'Sound Studio') { await page.locator('#voice-title').waitFor(); await page.getByRole('slider', { name: 'Tuning', exact: true }).waitFor(); }
      else await page.locator('.species-list').waitFor();
      await page.evaluate(() => document.fonts.ready);
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${name} overflows at ${width}`);
      await page.screenshot({ path: `test-results/demo-${name.replace(' ', '-').toLowerCase()}-${width}.png` });
    }
  }
  const tuning = page.getByRole('slider', { name: 'Tuning', exact: true });
  const before = Number(await tuning.getAttribute('aria-valuenow'));
  await tuning.focus(); await page.keyboard.press('ArrowUp');
  assert.ok(Number(await tuning.getAttribute('aria-valuenow')) > before);
  await visible('Unsaved changes');
  await page.getByRole('button', { name: 'Save voice', exact: true }).click();
  await page.getByText(/^Saved rev /).waitFor();
  checks.push('Demo keyboard parameter edit and mock voice save');
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.reload(); await visible('DEMO');
  await screen('Atlas');
  await page.getByRole('button', { name: 'Map details and coverage', exact: true }).click();
  assert.equal(await page.getByRole('button', { name: 'Reduce motion', exact: true }).getAttribute('aria-pressed'), 'true');
  await page.keyboard.press('Escape');
  console.log('CHECK', checks.length + 1); checks.push('Explicit demo, keyboard map control, both screens at 1280/1440/1920 widths, reduced motion');
  const fallback = await browser.newPage();
  await fallback.addInitScript(() => {
    const original = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, ...args) {
      return type.startsWith('webgl') ? null : original.call(this, type, ...args);
    };
  });
  await fallback.goto(origin);
  await fallback.getByText('WebGL is not available here, so the globe is replaced by a flat map.').waitFor();
  assert.equal(await fallback.locator('.map-stage canvas').count(), 0);
  await fallback.close(); console.log('CHECK', checks.length + 1); checks.push('Startup without WebGL uses 2D');
  const post = (body, headers = {}) => fetch(origin + '/__atlas/request', { method: 'POST', headers: { Origin: origin, 'Content-Type': 'application/json', ...headers }, body });
  assert.equal((await post('{}', { Origin: 'https://example.invalid' })).status, 403);
  assert.equal((await fetch(origin + '/__atlas/request', { headers: { Origin: origin } })).status, 405);
  assert.equal((await post('x'.repeat(16385))).status, 413);
  assert.equal((await post('{')).status, 400);
  const unsupported = await (await post(JSON.stringify({ protocolVersion: 1, requestId: 'browser-unsupported', method: 'setMasterGain', args: { gainDb: 0 } }))).json();
  assert.equal(unsupported.ok, false); assert.equal(unsupported.error.code, 'unsupported');
  console.log('CHECK', checks.length + 1); checks.push('HTTP origin, verb, size, malformed JSON and unsupported-mutation rejection');
  assert.deepEqual(errors, []); assert.deepEqual(external, []);
  console.log('CHECK', checks.length + 1); checks.push('No uncaught page errors or external HTTP requests in exercised native/demo flows');
  await writeFile('test-results/browser-checks.json', JSON.stringify({ checks, browser: browser.version(), errors, external }, null, 2) + '\n');
  console.log(JSON.stringify({ checks }, null, 2));
} catch (error) { console.error((await page.locator('body').innerText()).slice(0,6000)); throw error; } finally { await browser.close(); }
