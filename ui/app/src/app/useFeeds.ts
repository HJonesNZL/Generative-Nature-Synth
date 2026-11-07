/**
 * Data feeds into the bounded event stores.
 *
 * Live: pages through getChanges from the durable cursor. The cursor survives
 * screen switches (store is above the screen router) so returning to Atlas or
 * reconnecting the bridge resumes, never replays, and baseline/corrected
 * records never pulse. Wake-ups come from the provisional `changesAvailable`
 * notification when offered; otherwise a 2 s display poll. Neither is a
 * musical clock — sound timing is entirely host-side.
 *
 * Material: loads replay-session or frozen/queried history events in pages.
 */
import { EVENT_DISPLAY_CAPACITY } from "./eventStore";
import { useEffect } from "react";
import { useHost } from "./HostContext";
import { useAppState } from "./store";

const PAGE = 200;

export function useLiveFeed() {
  const { client, caps, connection, liveStore, onNotification } = useHost();
  useEffect(() => {
    if (!caps || connection !== "connected") return;
    if (caps.libraryAvailable === false) { liveStore.clear(); liveStore.setFeed({ status: "unsupported" }); return; }
    if (!client.supports("getChanges")) { liveStore.setFeed({ status: "unsupported" }); return; }
    let cancelled = false;
    let timer = 0;
    let backoff = 1000;
    let inFlight = false;
    let wakePending = false;
    const controller = new AbortController();

    const pull = async () => {
      window.clearTimeout(timer);
      if (cancelled) return;
      if (inFlight) { wakePending = true; return; }
      if (document.hidden) { timer = window.setTimeout(pull, 2000); return; }
      inFlight = true;
      wakePending = false;
      if (liveStore.feed.status === "idle") liveStore.setFeed({ status: "loading" });
      const { storeId, nextSequence } = liveStore.cursor;
      const result = await client.call("getChanges", { storeId, afterSequence: nextSequence, limit: PAGE }, { signal: controller.signal });
      inFlight = false;
      if (cancelled) return;
      if (!result.ok) {
        if (result.error.code === "aborted") return;
        liveStore.setFeed({ status: "retrying", error: result.error.message });
        timer = window.setTimeout(pull, backoff);
        backoff = Math.min(15000, backoff * 2);
        return;
      }
      backoff = 1000;
      const { value } = result;
      if (liveStore.cursor.storeId && value.storeId !== liveStore.cursor.storeId) {
        // Store identity changed (host reset or different library): start a fresh baseline.
        liveStore.clear();
      }
      liveStore.cursor = { storeId: value.storeId, nextSequence: value.nextSequence };
      liveStore.upsert(value.events.map(({ event, liveEligible, change }) => ({ event, pulse: liveEligible && change === "new" })));
      liveStore.requestOutcomes(client, value.events.map((e) => e.event.id));
      liveStore.setFeed({ status: "running", error: null, lastBatchWallMs: value.events.length ? Date.now() : liveStore.feed.lastBatchWallMs });
      timer = window.setTimeout(pull, wakePending || value.events.length >= PAGE ? 0 : 2000);
    };

    const off = onNotification((n) => { if (n.type === "changesAvailable") void pull(); });
    void pull();
    return () => { cancelled = true; controller.abort(); window.clearTimeout(timer); off(); };
  }, [client, caps, connection, liveStore, onNotification]);
}

export function useMaterialFeed() {
  const { client, caps, connection, materialStore } = useHost();
  const { mode, replay, history, materialReload } = useAppState();
  const material = mode === "replay" && replay.session
    ? { kind: "session" as const, sessionId: replay.session.id }
    : mode === "history" && history.result?.selectionId
      ? { kind: "selection" as const, selectionId: history.result.selectionId }
      : null;
  const key = material ? JSON.stringify(material) : "";

  useEffect(() => {
    materialStore.clear();
    if (!material || !caps || connection !== "connected") return;
    if (!client.supports("getMaterialEvents")) { materialStore.setFeed({ status: "unsupported" }); return; }
    const controller = new AbortController();
    (async () => {
      materialStore.setFeed({ status: "loading", error: null });
      let offset = 0;
      let revision: string | undefined;
      for (let page = 0; page < EVENT_DISPLAY_CAPACITY / 500; page += 1) {
        const result = await client.call("getMaterialEvents", { material, offset, limit: 500, ...(revision ? { revision } : {}) }, { signal: controller.signal });
        if (controller.signal.aborted) return;
        if (!result.ok) {
          // A stale revision invalidates every accumulated page, including map
          // markers and counts. Keep the failure visible without showing a mix.
          if (result.error.code === "stale_generation") materialStore.clear();
          materialStore.setFeed({ status: "error", error: result.error.message });
          return;
        }
        if (revision && result.value.revision !== revision) {
          materialStore.clear();
          materialStore.setFeed({ status: "error", error: "Library changed between pages. Reload this piece." });
          return;
        }
        revision = result.value.revision;
        materialStore.upsert(result.value.events.map((event) => ({ event, pulse: false })));
        materialStore.requestOutcomes(client, result.value.events.map((e) => e.id));
        offset += result.value.events.length;
        materialStore.setFeed({ totalRecords: result.value.total, displayOmitted: Math.max(0, result.value.total - materialStore.size) });
        if (offset >= result.value.total) break;
        if (!result.value.events.length) { materialStore.setFeed({ status: "error", error: "Incomplete material page. Retry loading this material." }); return; }
      }
      materialStore.setFeed({ status: "running", error: null });
    })();
    return () => controller.abort();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, caps, connection, materialReload]);
}
