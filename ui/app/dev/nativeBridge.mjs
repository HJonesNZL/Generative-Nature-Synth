import { spawn } from 'node:child_process';
import { resolve } from 'node:path';

/** Development-only worker transport; no SQL, provider access or data semantics in JS. */
export function nativeBridge() {
  let worker;
  let output = '';
  let activeId = null;
  const pending = new Map();
  const fail = () => {
    const previous = worker; worker = undefined; output = ''; activeId = null;
    for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(new Error('Native worker unavailable')); }
    pending.clear(); previous?.kill();
  };
  const pump = () => {
    if (!worker || activeId !== null || pending.size === 0) return;
    const [id, entry] = pending.entries().next().value;
    activeId = id;
    worker.stdin.write(entry.line);
  };
  const start = () => {
    if (worker) return;
    if (!process.env.ATLAS_LIBRARY) throw new Error('Configure ATLAS_LIBRARY before starting the preview');
    const child = spawn(resolve(process.env.ATLAS_NATIVE_BINARY || '../../build/native/atlas-native'),
      ['ui-host', '--db', resolve(process.env.ATLAS_LIBRARY)], { stdio: ['pipe', 'pipe', 'pipe'] });
    worker = child;
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (chunk) => {
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
    // Do not forward raw native diagnostic text/paths into the browser.
    child.stderr.resume();
    child.on('error', () => { if (worker === child) fail(); });
    child.on('exit', () => { if (worker === child) fail(); });
    child.stdin.on('error', () => { if (worker === child) fail(); });
  };
  return {
    name: 'atlas-native-development-bridge',
    apply: 'serve',
    configureServer(server) {
      server.httpServer?.once('close', fail);
      server.middlewares.use('/__atlas/request', (req, res) => {
        res.setHeader('Cache-Control', 'no-store');
        res.setHeader('X-Content-Type-Options', 'nosniff');
        const reject = (status) => { if (!res.writableEnded) { res.statusCode = status; res.end(); } };
        const address = server.httpServer?.address();
        const port = address && typeof address !== 'string' ? address.port : 0;
        const host = req.headers.host;
        if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress) ||
            ![`127.0.0.1:${port}`, `localhost:${port}`].includes(host) || req.headers.origin !== `http://${host}`) {
          reject(403); req.resume(); return;
        }
        if (req.method !== 'POST' || req.headers['content-type']?.split(';')[0] !== 'application/json') {
          reject(405); req.resume(); return;
        }
        let bytes = 0; const chunks = [];
        req.on('data', (chunk) => {
          bytes += chunk.length;
          if (bytes > 16384) { reject(413); return; }
          chunks.push(chunk);
        });
        req.on('end', async () => {
          if (res.writableEnded) return;
          try {
            let request;
            try { request = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(chunks))); }
            catch { reject(400); return; }
            if (!request || typeof request.requestId !== 'string' || !request.requestId || request.requestId.length > 128 ||
                pending.has(request.requestId)) { reject(400); return; }
            if (pending.size >= 16) { reject(429); return; }
            start();
            const result = await new Promise((resolveReply, rejectReply) => {
              const timer = setTimeout(fail, 15000);
              pending.set(request.requestId, { resolve: resolveReply, reject: rejectReply, timer, line: JSON.stringify(request) + '\n' });
              // Discard cancelled work that has not reached C++; keep the active reply correlated.
              res.once('close', () => {
                if (res.writableEnded || activeId === request.requestId) return;
                const entry = pending.get(request.requestId);
                if (!entry) return;
                pending.delete(request.requestId); clearTimeout(entry.timer);
                entry.reject(new Error('Request cancelled'));
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
    closeBundle: fail,
  };
}
