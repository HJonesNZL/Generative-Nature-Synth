import { spawn } from 'node:child_process';
import { resolve } from 'node:path';
import { existsSync, constants } from 'node:fs';
import { lstat, open } from 'node:fs/promises';

/** Development-only worker transport. The native host owns audio and a 5-second
 * getTransport lease; browser disappearance must never leave sounding playback.
 * One bounded FIFO owns the child, with no automatic retry of mutations. */
export function nativeBridge() {
  // One absolute directory is shared by native publication and HTTP delivery.
  const exportDirectory = resolve(process.env.ATLAS_EXPORT_DIRECTORY || '../../Exports');
  let worker;
  let output = '';
  let activeId = null;
  let closed = false;
  const pending = new Map();
  const fail = () => {
    const previous = worker; worker = undefined; output = ''; activeId = null;
    for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(new Error('Native worker unavailable')); }
    pending.clear();
    // stdin EOF is the native teardown contract; TERM bounds blocked teardown.
    if (previous) { previous.stdin.destroy(); previous.kill('SIGTERM'); }
  };
  const pump = () => {
    if (!worker || activeId !== null || pending.size === 0) return;
    const [id, entry] = pending.entries().next().value;
    activeId = id;
    worker.stdin.write(entry.line);
  };
  const start = () => {
    if (closed) throw new Error('Bridge closed');
    if (worker) return;
    if (!process.env.ATLAS_LIBRARY) throw new Error('Configure ATLAS_LIBRARY before starting the preview');
    const audioBinary = resolve('../../build/native-playback/atlas-audio-host');
    const binary = process.env.ATLAS_AUDIO_HOST_BINARY || process.env.ATLAS_NATIVE_BINARY ||
      (existsSync(audioBinary) ? audioBinary : '../../build/native/atlas-native');
    const child = spawn(resolve(binary), ['ui-host', '--db', resolve(process.env.ATLAS_LIBRARY)], { stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, ATLAS_VOICE_LIBRARY: process.env.ATLAS_VOICE_LIBRARY || resolve('../../.state/voices.sqlite3'), ATLAS_EXPORT_DIRECTORY: exportDirectory } });
    worker = child;
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (chunk) => {
      // Late output/exit from a retired process cannot affect its replacement.
      if (worker !== child) return;
      output += chunk;
      if (Buffer.byteLength(output) > 8 * 1024 * 1024) { fail(); return; }
      let end;
      while ((end = output.indexOf('\n')) >= 0) {
        const line = output.slice(0, end); output = output.slice(end + 1);
        try {
          const reply = JSON.parse(line), entry = pending.get(reply.requestId);
          if (!entry || reply.requestId !== activeId) throw new Error('Uncorrelated native reply');
          pending.delete(reply.requestId); clearTimeout(entry.timer); activeId = null; entry.resolve(reply); pump();
        } catch { fail(); return; }
      }
    });
    child.stderr.resume(); // Never forward raw native diagnostics/paths into the browser.
    child.on('error', () => { if (worker === child) fail(); });
    child.on('exit', () => { if (worker === child) fail(); });
    child.stdin.on('error', () => { if (worker === child) fail(); });
  };
  const shutdown = () => { closed = true; fail(); };
  return {
    name: 'atlas-native-development-bridge', apply: 'serve',
    configureServer(server) {
      server.httpServer?.once('close', shutdown);
      process.once('exit', shutdown);
      const signal = () => { shutdown(); process.exit(0); };
      process.once('SIGINT', signal); process.once('SIGTERM', signal);
      server.httpServer?.once('close', () => {
        process.removeListener('exit', shutdown); process.removeListener('SIGINT', signal); process.removeListener('SIGTERM', signal);
      });
      server.middlewares.use('/__atlas/exports', async (req, res) => {
        res.setHeader('Cache-Control', 'no-store');
        res.setHeader('X-Content-Type-Options', 'nosniff');
        const reject = status => { res.statusCode = status; res.end(); req.resume(); };
        const address = server.httpServer?.address();
        const port = address && typeof address !== 'string' ? address.port : 0;
        const host = req.headers.host;
        if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress) ||
            ![`127.0.0.1:${port}`, `localhost:${port}`].includes(host) ||
            (req.headers.origin && req.headers.origin !== `http://${host}`) ||
            (req.headers['sec-fetch-site'] && req.headers['sec-fetch-site'] !== 'same-origin')) { reject(403); return; }
        if (req.method !== 'GET') { reject(405); return; }
        const match = /^\/([0-9a-f]{32})$/.exec(req.url || '');
        if (!match) { reject(404); return; }
        const directory = exportDirectory;
        let file;
        try {
          const folder = await lstat(directory);
          if (!folder.isDirectory() || folder.isSymbolicLink()) { reject(404); return; }
          file = await open(resolve(directory, `${match[1]}.wav`), constants.O_RDONLY | constants.O_NOFOLLOW);
          const stat = await file.stat();
          if (!stat.isFile()) { await file.close(); reject(404); return; }
          res.setHeader('Content-Type', 'audio/wav');
          res.setHeader('Content-Disposition', `attachment; filename="Animal-Atlas-${match[1]}.wav"`);
          res.setHeader('Content-Length', String(stat.size));
          const stream = file.createReadStream();
          stream.once('error', () => res.destroy());
          res.once('close', () => stream.destroy());
          stream.pipe(res);
        } catch { await file?.close().catch(() => {}); if (!res.headersSent) reject(404); else res.destroy(); }
      });
      server.middlewares.use('/__atlas/request', (req, res) => {
        res.setHeader('Cache-Control', 'no-store');
        res.setHeader('X-Content-Type-Options', 'nosniff');
        const reject = (status) => { if (!res.writableEnded) { res.statusCode = status; res.end(); } };
        const address = server.httpServer?.address();
        const port = address && typeof address !== 'string' ? address.port : 0;
        const host = req.headers.host;
        if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress) ||
            ![`127.0.0.1:${port}`, `localhost:${port}`].includes(host) || req.headers.origin !== `http://${host}` ||
            (req.headers['sec-fetch-site'] && req.headers['sec-fetch-site'] !== 'same-origin')) {
          reject(403); req.resume(); return;
        }
        if (req.method !== 'POST' || req.headers['content-type']?.split(';')[0] !== 'application/json') {
          reject(405); req.resume(); return;
        }
        let bytes = 0; const chunks = [];
        req.on('data', (chunk) => {
          if (res.writableEnded) return;
          bytes += chunk.length;
          if (bytes > 16384) { chunks.length = 0; reject(413); return; }
          chunks.push(chunk);
        });
        req.on('end', async () => {
          if (res.writableEnded) return;
          try {
            let request;
            try { request = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(chunks))); }
            catch { reject(400); return; }
            if (!request || typeof request !== 'object' || Array.isArray(request) || request.protocolVersion !== 1 ||
                typeof request.requestId !== 'string' || !request.requestId || request.requestId.length > 128 ||
                typeof request.method !== 'string' || !/^[a-zA-Z][a-zA-Z0-9]{0,63}$/.test(request.method) ||
                !request.args || typeof request.args !== 'object' || Array.isArray(request.args) ||
                Object.keys(request).some(key => !['protocolVersion', 'requestId', 'method', 'args'].includes(key)) ||
                pending.has(request.requestId)) { reject(400); return; }
            // C++ validates method-specific DTOs and capabilities. This queue never retries.
            if (pending.size >= 16) { reject(429); return; }
            start();
            const result = await new Promise((resolveReply, rejectReply) => {
              const timer = setTimeout(fail, ['saveSoundscape', 'recallSoundscape', 'setComposition', 'setSpatial'].includes(request.method) ? 30000 : 5000);
              pending.set(request.requestId, { resolve: resolveReply, reject: rejectReply, timer, line: JSON.stringify(request) + '\n' });
              res.once('close', () => {
                if (res.writableEnded) return;
                if (activeId === request.requestId) {
                  // Reads and preparation may finish into a discarded response;
                  // preserve FIFO correlation for the replacement selection.
                  // An abandoned audible mutation retires the process immediately.
                  if (!['getCapabilities', 'getChanges', 'getCatalog', 'listSessions', 'getMaterialEvents', 'getTransport', 'getAudioState', 'getVoiceLibrary', 'getVoice', 'getComposition', 'getSpatial', 'prepareReplay', 'listSoundscapes', 'getRenderJob'].includes(request.method)) fail();
                  return;
                }
                const entry = pending.get(request.requestId);
                if (!entry) return;
                pending.delete(request.requestId); clearTimeout(entry.timer); entry.reject(new Error('Request cancelled'));
              });
              pump();
            });
            if (res.destroyed) return;
            res.setHeader('Content-Type', 'application/json; charset=utf-8'); res.end(JSON.stringify(result));
          } catch { reject(503); }
        });
        req.on('error', () => reject(400));
      });
    },
    closeBundle: shutdown,
  };
}
