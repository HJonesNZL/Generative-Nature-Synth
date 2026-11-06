// Run after both builds, served locally by vite preview on ports 4173 and 4174.
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const nativeUrl = process.env.ATLAS_STATIC_URL || 'http://127.0.0.1:4173/';
const demoUrl = process.env.ATLAS_STATIC_DEMO_URL || 'http://127.0.0.1:4174/demo.html';
const browser = await chromium.launch({ headless: true, channel: process.env.ATLAS_BROWSER || 'chrome', args: ['--enable-unsafe-swiftshader'] });
const errors = [], requests = [], failures = [];
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
page.on('pageerror', error => errors.push(error.message));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
page.on('request', request => requests.push(request.url()));
page.on('response', response => { if (response.status() >= 400) failures.push(response.url()); });
try {
  await page.goto(nativeUrl);
  await page.getByText('Host unavailable', { exact: true }).waitFor();
  await page.getByText('Native host is not connected. This build does not substitute demo data.', { exact: true }).waitFor();
  assert.equal(await page.getByText('DEMO', { exact: true }).count(), 0);
  assert.equal(await page.getByRole('slider', { name: 'Master level' }).isDisabled(), true);
  await page.goto(demoUrl);
  await page.getByText('DEMO', { exact: true }).waitFor();
  await page.locator('.map-stage canvas').waitFor();
  await page.getByRole('button', { name: 'Sound Studio', exact: true }).click();
  await page.locator('#voice-title').waitFor();
  await page.getByRole('slider', { name: 'Tuning', exact: true }).waitFor();
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: 'test-results/static-demo-studio.png' });
  assert.deepEqual(errors, []); assert.deepEqual(failures, []);
  assert.ok(requests.every(url => url.startsWith(new URL(nativeUrl).origin) || url.startsWith(new URL(demoUrl).origin) || url.startsWith('data:')));
  assert.ok(requests.every(url => !url.includes('/__atlas/')));
  const result = { browser: browser.version(), errors, failures, checks: ['Production without native injection fails closed', 'Static demo globe and Studio load with CSP and local assets', 'No external HTTP or development bridge requests'] };
  await writeFile('test-results/static-browser-checks.json', JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
} finally { await browser.close(); }
