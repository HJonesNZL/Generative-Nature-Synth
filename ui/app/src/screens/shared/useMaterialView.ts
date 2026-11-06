/**
 * Derives what both screens display from the active material:
 * live arrivals (last 60 min), a replay session, or a history selection.
 * Recomputed once per batched store version (not per event).
 */
import { useEffect, useMemo, useState } from "react";
import type { EventView } from "../../host/contracts";
import { useHost } from "../../app/HostContext";
import { useEventStore, type EventStore } from "../../app/eventStore";
import { useAppState } from "../../app/store";

const LIVE_WINDOW_MS = 60 * 60_000;
const RECENT_MS = 10 * 60_000;

export interface MaterialView {
  store: EventStore;
  version: number;
  events: EventView[];          // in window, oldest first
  allEvents: EventView[];
  windowFromMs: number;
  windowToMs: number;
  recentFromMs: number;
  timeOf: (e: EventView) => number;
  feed: EventStore["feed"];
  label: string;
}

export function useMaterialView(): MaterialView {
  const { liveStore, materialStore } = useHost();
  const state = useAppState();
  const store = state.mode === "live" ? liveStore : materialStore;
  const version = useEventStore(store);
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (state.mode !== "live") return;
    const id = window.setInterval(() => setNow(Date.now()), 10_000);
    return () => window.clearInterval(id);
  }, [state.mode]);

  return useMemo(() => {
    const basis = state.timeBasis;
    const timeOf = (e: EventView) => Date.parse(basis === "observed" ? e.observedAtUtc : e.firstSeenAtUtc);
    const allEvents = store.all();
    let from: number;
    let to: number;
    let label: string;
    if (state.mode === "live") {
      to = Math.max(now, ...allEvents.slice(-1).map(timeOf));
      from = to - LIVE_WINDOW_MS;
      label = "Last 60 min";
    } else if (state.mode === "history" && state.history.result) {
      from = Date.parse(state.history.result.requestedFromUtc);
      to = Date.parse(state.history.result.requestedToUtc);
      label = "Requested window";
    } else {
      const times = allEvents.map(timeOf);
      const bounds = basis === "observed" ? state.replay.session?.observedBoundsUs : state.replay.session?.arrivalBoundsUs;
      from = bounds ? Number(BigInt(bounds[0]) / 1000n) : times.length ? Math.min(...times) : now - LIVE_WINDOW_MS;
      to = bounds ? Number(BigInt(bounds[1]) / 1000n) : times.length ? Math.max(...times) : now;
      label = state.mode === "replay" ? state.replay.session?.id.startsWith("scene:") ? "Saved soundscape" : "Saved piece" : "No selection";
    }
    const events = allEvents.filter((e) => { const t = timeOf(e); return t >= from && t <= to; }).sort((a, b) => timeOf(a) - timeOf(b));
    return { store, version, events, allEvents, windowFromMs: from, windowToMs: to, recentFromMs: state.mode === "live" ? to - RECENT_MS : from, timeOf, feed: store.feed, label };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [store, version, now, state.mode, state.timeBasis, state.history.result, state.replay.session]);
}

export function markerKeyForEvent(e: EventView | null): string | null {
  if (!e) return null;
  if (e.eventKind === "satellite_fix" || e.eventKind === "gps_fix") return `fix:${e.id}`;
  return e.stationId ? `station:${e.stationId}` : null;
}
