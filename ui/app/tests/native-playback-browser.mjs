// Requires the disposable synthetic library and atlas-playback-fixture-host.
// Exercises real UiHost/PlaybackRuntime/DSP via a silent test output driver.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
page.setDefaultTimeout(10000);
const errors = [], rejected = [], mutations = [], timeline = [], cancelledReads = [], preparations = [];
page.on('pageerror', error => errors.push(error.message));
page.on('response', async response => {
  if (!response.url().includes('/__atlas/request')) return;
  const request = response.request().postDataJSON();
  try {
    const reply = await response.json();
    if (!reply.ok) rejected.push({ method: request.method, error: reply.error });
    if (request.method === 'setTransport') mutations.push(request.args.action);
    if (request.method === 'prepareReplay') preparations.push(request.args.sessionId);
    if (request.method === 'getTransport' && reply.ok) timeline.push(reply.result);
  } catch {
    if (response.status() === 200 && !['setTransport', 'setAudioEnabled', 'prepareReplay'].includes(request.method) && await response.finished().catch(() => 'browser closed')) cancelledReads.push(request.method);
    else rejected.push({ method: request.method, status: response.status() });
  }
});
const button = name => page.getByRole('button', { name, exact: true });
const radio = name => page.getByRole('radio', { name, exact: true });
const listening = () => page.getByRole('switch', { name: /Listening/ });
try {
  await page.goto(process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5182');
  await page.getByLabel('Project', { exact: true }).click();
  await page.locator('.project-tools__help summary').click();
  await page.getByText('Open a saved piece or soundscape, enable Listening, then press Play. Save a soundscape to preserve its events, voices and composition; WAV export uses the saved version.', { exact: true }).waitFor();
  await page.keyboard.press('Escape');
  await radio('Replay').click();
  await page.getByRole('button', { name: new RegExp(process.env.ATLAS_FIXTURE_PIECE || 'Synthetic two second piece') }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  await radio('1×').click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  await listening().click();
  await button('Play').click(); await button('Pause playback').waitFor();
  await page.waitForTimeout(550);
  await button('Pause playback').click(); await button('Play').waitFor();
  await button('Play').click(); await button('Pause playback').waitFor();
  const preparedCount = preparations.length;
  await button('Sound Studio').click(); await button('Atlas').click();
  assert.equal(preparations.length, preparedCount);
  await button('Return to start').click(); await button('Play').waitFor();
  await page.waitForFunction(() => !document.querySelector('button[aria-label="Return to start"]')?.disabled);
  assert.ok(mutations.slice(-2).join(',') === 'pause,seek', JSON.stringify(mutations));
  await button('Play').click(); await button('Pause playback').waitFor();
  await button('Stop playback').click(); await button('Play').waitFor();
  await button('Play').click(); await button('Pause playback').waitFor();
  await button('Play').waitFor({ timeout: 15000 }); // Natural completion, including native tail.
  await page.waitForTimeout(100);
  assert.ok(timeline.some(s => s.state === 'stopped' && s.positionUs === s.durationUs && s.durationUs !== '0'), JSON.stringify(timeline.slice(-3)));
  await button('Play').click(); await button('Pause playback').waitFor();
  assert.ok(mutations.slice(-2).join(',') === 'stop,play', JSON.stringify(mutations));
  await radio('10×').click(); await radio('240×').click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  await page.getByRole('button', { name: /Synthetic short piece/ }).click();
  await page.getByText('Ready — use Play in the header.', { exact: true }).waitFor();
  assert.equal(await button('Play').isDisabled(), true);
  await listening().click(); await button('Play').click(); await button('Pause playback').waitFor();
  await radio('Live').click();
  await page.getByRole('button', { name: 'Play (not available for live arrivals)' }).waitFor();
  await page.waitForFunction(() => document.querySelector('[role="switch"][aria-label="Listening (audio off)"]')?.getAttribute('aria-checked') === 'false');
  await page.waitForTimeout(600);
  assert.ok(timeline.at(-1).state === 'stopped');
  assert.deepEqual(errors, []); assert.deepEqual(rejected, []);
  console.log(JSON.stringify({ checks: ['actual native prepare/device-enable generation refresh/play', 'native polling/pause/resume/stop and screen continuity', 'return-to-start while sounding', 'natural completion with tail and replay', 'rapid speed and piece replacement stay disabled', 'live selection stops/disables saved audio'], rejected, cancelledReads, pageErrors: errors }));
} catch (error) { console.error(JSON.stringify({ alerts: await page.getByRole("alert").allTextContents(), mutations, rejected, timeline: timeline.slice(-5) })); throw error; } finally { page.removeAllListeners("response"); await browser.close(); }
