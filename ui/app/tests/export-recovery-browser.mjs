// Synthetic injected host: no data library, real render, audio output or HTTP bridge.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5178';
const output = resolve(process.env.ATLAS_EXPORT_RECOVERY_EVIDENCE || 'test-results/export-recovery');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
const errors = [], external = [], bridgeRequests = [], failedResponses = [], checks = [];
context.on('page', page => page.on('pageerror', error => errors.push(error.message)));
context.on('request', request => {
  if (/^https?:/.test(request.url()) && new URL(request.url()).origin !== new URL(origin).origin) external.push(request.url());
  if (request.url().includes('/__atlas/')) bridgeRequests.push(request.url());
});
context.on('response', response => { if (response.status() >= 400) failedResponses.push({ url: response.url(), status: response.status() }); });
await context.addInitScript(() => {
  const key = 'atlas-export-recovery-cancelled';
  const mock = new URL(location.href).searchParams.get('fixture') === 'mock';
  const sceneId = 'a'.repeat(32), jobId = 'b'.repeat(32);
  const fixture = window.exportRecoveryFixture = { calls: [], histories: [], mock, completed: false };
  const cancelled = () => sessionStorage.getItem(key) === 'true';
  const job = () => ({ jobId, soundscapeId: sceneId, state: fixture.completed ? 'complete' : cancelled() ? 'cancelled' : 'rendering', progress: fixture.completed ? 1 : .5, message: null, ...(fixture.completed ? {artifactId:'d'.repeat(32)} : {}) });
  const scene = { id: sceneId, name: 'Synthetic saved scene', valid: true, contentVerified: true, error: null, contentHash: 'c'.repeat(64), sourceSessionId: 'synthetic-source', eventCount: 0, durationUs: '1000000', basis: 'observed', speedNumerator: '1', speedDenominator: '1' };
  const methods = ['getCapabilities', 'listExports', 'listSoundscapes', 'getRenderJob', 'cancelRender'];
  window.atlasHost = { request: async request => {
    fixture.calls.push({ method: request.method, args: request.args });
    let result;
    if (request.method === 'getCapabilities') result = { protocolVersion: 1, implementation: mock ? 'mock' : 'native', supportedMethods: methods, audioAvailable: false, libraryAvailable: false, maxEventBatch: 500, maxVisibleMarkers: 400 };
    else if (request.method === 'listSoundscapes') result = { items: [scene], limited: false, unmanagedFiles: 0 };
    else if (request.method === 'listExports') {
      result = { items: [], limited: false, unavailableFiles: 0, incompleteRenders: mock || cancelled() || fixture.completed ? 0 : 1, activeRender: mock || cancelled() || fixture.completed ? null : job() };
      fixture.histories.push(structuredClone(result));
    } else if (request.method === 'getRenderJob') {
      if (request.args.jobId !== jobId) throw Error('Unexpected synthetic render identity');
      result = job();
    } else if (request.method === 'cancelRender') {
      if (request.args.jobId !== jobId) throw Error('Unexpected cancellation identity');
      sessionStorage.setItem(key, 'true');
      sessionStorage.setItem('atlas-export-recovery-cancel-count', String(Number(sessionStorage.getItem('atlas-export-recovery-cancel-count') || 0) + 1));
      result = job();
    } else throw Error(`Unexpected synthetic export recovery method: ${request.method}`);
    return { requestId: request.requestId, ok: true, result: structuredClone(result) };
  } };
});

const page = await context.newPage();
page.setDefaultTimeout(10000);
const project = page.locator('details.project-tools');
const projectSummary = page.getByLabel('Project', { exact: true });
const progress = page.getByRole('progressbar', { name: 'WAV export progress', exact: true });
const cancel = page.getByRole('button', { name: 'Cancel export', exact: true });
const exportLinks = target => target.locator('a[data-artifact-id]');
const exporting = target => target.locator('.project-tools__state').filter({ hasText: /^Exporting$/ });
async function assertNoCompletion(target) {
  assert.equal(await target.getByText('WAV export complete.', { exact: false }).count(), 0);
  assert.equal(await target.locator('.project-tools__state').filter({ hasText: /^WAV ready$/ }).count(), 0);
  assert.equal(await exportLinks(target).count(), 0, 'no download link without a completed artifact');
}

try {
  await mkdir(output, { recursive: true });
  await page.goto(origin);
  await exporting(page).waitFor();
  assert.equal(await project.evaluate(element => element.open), false, 'recovered job is visible in header without expanding Project');
  assert.equal(await progress.isVisible(), false, 'secondary progress remains inside collapsed tools');
  await page.waitForFunction(() => exportRecoveryFixture.calls.some(call => call.method === 'getRenderJob'));
  assert.equal(await page.evaluate(() => exportRecoveryFixture.histories[0].activeRender.state), 'rendering');
  assert.equal(await page.evaluate(() => exportRecoveryFixture.histories[0].incompleteRenders), 1);
  await assertNoCompletion(page);
  await page.screenshot({ path: resolve(output, 'recovered-collapsed.png') });
  await projectSummary.click(); await progress.waitFor();
  assert.equal(await progress.getAttribute('value'), '0.5');
  assert.equal(await cancel.isEnabled(), true);
  await page.locator('.export-history summary').click();
  await page.getByText('1 unfinished render files. These may belong to an active export.', { exact: true }).waitFor();
  await page.screenshot({ path: resolve(output, 'recovered-progress.png') });
  checks.push('Existing rendering job is recovered from listExports while Project stays collapsed; opening reveals actual progress and cancellation');

  await cancel.click();
  await page.getByText('WAV export cancelled.', { exact: true }).waitFor();
  await page.waitForFunction(() => exportRecoveryFixture.histories.some(history => history.activeRender === null && history.incompleteRenders === 0));
  assert.equal(await exporting(page).count(), 0);
  assert.equal(await cancel.count(), 0);
  assert.equal(await progress.count(), 0);
  assert.equal(await page.evaluate(() => exportRecoveryFixture.calls.filter(call => call.method === 'cancelRender').length), 1);
  assert.equal(await page.evaluate(() => sessionStorage.getItem('atlas-export-recovery-cancel-count')), '1');
  await assertNoCompletion(page);
  await page.screenshot({ path: resolve(output, 'cancelled.png') });
  checks.push('Cancellation sends the recovered native job identity once and displays terminal cancellation without fabricating a WAV completion');

  await page.reload();
  await page.waitForFunction(() => exportRecoveryFixture.histories.length > 0);
  assert.equal(await project.evaluate(element => element.open), false, 'reload keeps secondary tools collapsed');
  assert.equal(await page.evaluate(() => exportRecoveryFixture.histories[0].activeRender), null);
  assert.equal(await page.evaluate(() => exportRecoveryFixture.histories[0].incompleteRenders), 0);
  assert.equal(await exporting(page).count(), 0);
  assert.equal(await progress.count(), 0);
  assert.equal(await cancel.count(), 0);
  assert.equal(await page.evaluate(() => exportRecoveryFixture.calls.some(call => call.method === 'getRenderJob' || call.method === 'cancelRender')), false, 'reload does not recover a terminal job');
  assert.equal(await page.evaluate(() => sessionStorage.getItem('atlas-export-recovery-cancel-count')), '1');
  await projectSummary.click(); await page.locator('.export-history summary').click();
  await page.getByText('Completed WAV files will appear here, including after restarting.', { exact: true }).waitFor();
  await assertNoCompletion(page);
  await page.screenshot({ path: resolve(output, 'reloaded-without-active-export.png') });
  checks.push('Session-persisted cancellation survives reload; empty completed history and null activeRender do not resurrect polling, progress or download controls');

  const backgroundPage = await context.newPage(); backgroundPage.setDefaultTimeout(10000);
  await backgroundPage.addInitScript(() => Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }));
  await backgroundPage.goto(origin);
  await exporting(backgroundPage).waitFor();
  await backgroundPage.waitForFunction(() => exportRecoveryFixture.calls.filter(call => call.method === 'getRenderJob').length >= 1);
  await backgroundPage.evaluate(() => { exportRecoveryFixture.completed = true; });
  await backgroundPage.getByText('WAV ready', {exact:true}).waitFor();
  assert.equal(await backgroundPage.evaluate(() => document.hidden), true);
  const completedPolls = await backgroundPage.evaluate(() => exportRecoveryFixture.calls.filter(call => call.method === 'getRenderJob').length);
  await backgroundPage.waitForTimeout(1100);
  assert.equal(await backgroundPage.evaluate(() => exportRecoveryFixture.calls.filter(call => call.method === 'getRenderJob').length), completedPolls, 'terminal export disables its polling timer');
  assert.equal(await backgroundPage.locator('details.project-tools').evaluate(element => element.open), false);
  checks.push('Active explicit export polls while document.hidden and Project closed, reaches WAV ready, then stops polling');
  await backgroundPage.close();

  const mockPage = await context.newPage(); mockPage.setDefaultTimeout(10000);
  await mockPage.goto(`${origin}/?fixture=mock`);
  await mockPage.waitForFunction(() => exportRecoveryFixture.calls.some(call => call.method === 'listSoundscapes'));
  await mockPage.getByLabel('Project', { exact: true }).click();
  await mockPage.getByRole('combobox', { name: 'Saved soundscape', exact: true }).selectOption('a'.repeat(32));
  await mockPage.getByText('“Synthetic saved scene” · 0 records. Export uses this saved version.', { exact: true }).waitFor();
  assert.equal(await mockPage.getByRole('link', { name: 'Export scene', exact: true }).count(), 0, 'in-memory mock scenes have no filesystem export link');
  await assertNoCompletion(mockPage);
  await mockPage.screenshot({ path: resolve(output, 'mock-scene-export-hidden.png') });
  checks.push('Mock capabilities with a valid selected scene hide unsupported filesystem scene export');

  const calls = await page.evaluate(() => exportRecoveryFixture.calls);
  assert.ok(calls.every(call => ['getCapabilities', 'listExports', 'listSoundscapes'].includes(call.method)));
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(bridgeRequests, []); assert.deepEqual(failedResponses, []);
  const result = { fixture: 'Injected synthetic scene-only host; sessionStorage-persisted cancellation; no audio/data/filesystem bridge access', browser: browser.version(), checks, errors, external, bridgeRequests, failedResponses };
  await writeFile(resolve(output, 'results.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result));
} catch (error) {
  await page.screenshot({ path: resolve(output, 'failure.png') });
  console.error(JSON.stringify({ errors, body: await page.locator('body').innerText() }));
  throw error;
} finally { await browser.close(); }
