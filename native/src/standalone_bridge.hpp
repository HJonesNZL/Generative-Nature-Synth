#pragma once
#include <string_view>
namespace atlas::standalone {
inline constexpr std::string_view bridgeScript = R"ATLASJS(
(() => {
  'use strict';
  const backend = window.__JUCE__.backend;
  const pending = new Map();
  let next = 0;
  const failure = (id, code, message) => ({requestId: id, ok: false, error: {code, message}});
  backend.addEventListener('atlasReply', event => {
    const item = pending.get(event.token);
    if (!item) return;
    let reply;
    try {
      if (typeof event.json !== 'string' || event.json.length > 8388608) throw Error();
      reply = JSON.parse(event.json);
      if (reply.requestId !== item.id || typeof reply.ok !== 'boolean') throw Error();
    } catch { reply = failure(item.id, 'internal', 'Invalid native host reply.'); }
    item.finish(reply);
  });
  window.atlasHost = {
    request(request, signal) {
      const id = request?.requestId ?? '';
      if (signal?.aborted) return Promise.resolve(failure(id, 'aborted', 'Request cancelled.'));
      if (pending.size >= 16) return Promise.resolve(failure(id, 'unavailable', 'Native request queue is full.'));
      let json;
      try { json = JSON.stringify(request); } catch { return Promise.resolve(failure(id, 'invalid_request', 'Invalid request.')); }
      if (typeof json !== 'string' || new TextEncoder().encode(json).length > 16384)
        return Promise.resolve(failure(id, 'invalid_request', 'Request exceeds capacity.'));
      const token = ++next;
      return new Promise(resolve => {
        let timer;
        const abort = () => finish(failure(id, 'aborted', 'Request cancelled; an admitted edit may have completed.'));
        const finish = reply => {
          if (!pending.delete(token)) return;
          clearTimeout(timer);
          signal?.removeEventListener('abort', abort);
          resolve(reply);
        };
        pending.set(token, {id, finish});
        signal?.addEventListener('abort', abort, {once: true});
        timer = setTimeout(() => finish(failure(id, 'timeout', 'Native request timed out; an admitted edit may have completed.')), 30000);
        backend.emitEvent('atlasRequest', {token, json});
      });
    },
    subscribe() { return () => {}; }
  };
  document.addEventListener('click', event => {
    const link = event.target?.closest?.('a[data-artifact-id]');
    if (!link) return;
    event.preventDefault();
    const artifactId = link.dataset.artifactId;
    if (/^[0-9a-f]{32}$/.test(artifactId ?? '')) backend.emitEvent('atlasExport', {artifactId});
  }, true);
  window.addEventListener('pagehide', () => {
    for (const item of [...pending.values()]) item.finish(failure(item.id, 'unavailable', 'Native page closed.'));
  });
})();
)ATLASJS";
}
