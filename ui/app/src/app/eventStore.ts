/**
 * Bounded event buffer that lives OUTSIDE React state.
 *
 * Arrivals are appended in batches; subscribers are notified at most once per
 * animation frame via a version counter (useSyncExternalStore). This keeps a
 * burst of detections from causing one React render per event.
 *
 * Capacity: CAPACITY events (oldest evicted first). Pulse pool: PULSE_CAPACITY
 * recent live-eligible arrivals for map pings. Outcomes are cached per id.
 */
import { useSyncExternalStore } from "react";
import type { EventView } from "../host/contracts";
import type { MusicalOutcome } from "../host/provisional";
import type { HostClient } from "../host/client";

export const EVENT_DISPLAY_CAPACITY = 2000;
const CAPACITY = EVENT_DISPLAY_CAPACITY;
const PULSE_CAPACITY = 24;
const OUTCOME_BATCH = 200;

export interface Pulse { eventId: string; latitude: number; longitude: number; createdWallMs: number }

export class EventStore {
  private byId = new Map<string, EventView>();
  private order: string[] = [];
  private outcomes = new Map<string, MusicalOutcome>();
  private pulses: Pulse[] = [];
  private listeners = new Set<() => void>();
  private version = 0;
  private scheduled = false;
  private epoch = 0;
  private notificationHandle: number | null = null;
  private notificationIsTimer = false;
  private pendingOutcomeIds = new Set<string>();
  private outcomeTimer: number | null = null;
  cursor: { storeId: string; nextSequence: string } = { storeId: "", nextSequence: "0" };
  feed: { status: "idle" | "loading" | "running" | "retrying" | "error" | "unsupported"; error: string | null; lastBatchWallMs: number | null; totalRecords: number | null; displayOmitted: number } = { status: "idle", error: null, lastBatchWallMs: null, totalRecords: null, displayOmitted: 0 };

  constructor(private readonly label: string) {}

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  };
  getVersion = () => this.version;

  private notify() {
    if (this.scheduled) return;
    this.scheduled = true;
    const flush = () => {
      this.scheduled = false;
      this.notificationHandle = null;
      this.version += 1;
      for (const l of this.listeners) l();
    };
    this.notificationIsTimer = typeof document !== "undefined" && document.hidden;
    this.notificationHandle = this.notificationIsTimer ? window.setTimeout(flush, 250) : window.requestAnimationFrame(flush);
  }

  setFeed(feed: Partial<EventStore["feed"]>) {
    this.feed = { ...this.feed, ...feed };
    this.notify();
  }

  dispose() {
    this.epoch += 1;
    if (this.notificationHandle !== null) {
      if (this.notificationIsTimer) window.clearTimeout(this.notificationHandle);
      else window.cancelAnimationFrame(this.notificationHandle);
    }
    if (this.outcomeTimer !== null) window.clearTimeout(this.outcomeTimer);
    this.notificationHandle = null;
    this.outcomeTimer = null;
    this.scheduled = false;
    this.pendingOutcomeIds.clear();
  }

  clear() {
    this.dispose();
    this.outcomes.clear();
    this.feed = { status: "idle", error: null, lastBatchWallMs: null, totalRecords: null, displayOmitted: 0 };
    this.byId.clear();
    this.order = [];
    this.pulses = [];
    this.cursor = { storeId: "", nextSequence: "0" };
    this.notify();
  }

  /** Insert or replace (corrections replace the prior revision; they never re-pulse). */
  upsert(batch: Array<{ event: EventView; pulse: boolean }>) {
    if (!batch.length) return;
    const now = performance.now();
    for (const { event, pulse } of batch) {
      const previous = this.byId.get(event.id);
      if (previous && BigInt(previous.revisionSequence) >= BigInt(event.revisionSequence)) continue;
      if (!previous) this.order.push(event.id);
      this.byId.set(event.id, event);
      if (pulse && event.position) {
        this.pulses.push({ eventId: event.id, latitude: event.position.latitude, longitude: event.position.longitude, createdWallMs: now });
        if (this.pulses.length > PULSE_CAPACITY) this.pulses.splice(0, this.pulses.length - PULSE_CAPACITY);
      }
    }
    if (this.order.length > CAPACITY) {
      const evicted = this.order.splice(0, this.order.length - CAPACITY);
      this.feed.displayOmitted += evicted.length;
      for (const id of evicted) { this.byId.delete(id); this.outcomes.delete(id); }
    }
    this.notify();
  }

  get size() { return this.order.length; }
  get(id: string | null) { return id ? this.byId.get(id) ?? null : null; }
  /** Newest-last list; callers must not mutate. */
  all(): EventView[] { return this.order.map((id) => this.byId.get(id)!); }
  activePulses(maxAgeMs: number): Pulse[] {
    const now = performance.now();
    return this.pulses.filter((p) => now - p.createdWallMs < maxAgeMs);
  }
  outcome(id: string): MusicalOutcome | undefined { return this.outcomes.get(id); }

  /** Batch outcome lookups (provisional method). Missing support leaves outcomes undefined. */
  requestOutcomes(client: HostClient, ids: string[]) {
    if (!client.supports("getMusicalOutcomes")) return;
    for (const id of ids) if (!this.outcomes.has(id)) this.pendingOutcomeIds.add(id);
    if (this.outcomeTimer !== null || !this.pendingOutcomeIds.size) return;
    const epoch = this.epoch;
    this.outcomeTimer = window.setTimeout(async () => {
      this.outcomeTimer = null;
      const batch = [...this.pendingOutcomeIds].slice(0, OUTCOME_BATCH);
      for (const id of batch) this.pendingOutcomeIds.delete(id);
      const result = await client.call("getMusicalOutcomes", { eventIds: batch });
      if (epoch !== this.epoch) return;
      if (result.ok) {
        for (const [id, outcome] of Object.entries(result.value)) if (this.byId.has(id)) this.outcomes.set(id, outcome);
        this.notify();
      }
      if (this.pendingOutcomeIds.size) this.requestOutcomes(client, []);
    }, 120);
  }

  /** Re-fetch outcomes after mix/voice changes (outcomes are host-owned). */
  invalidateOutcomes(client: HostClient) {
    const ids = [...this.outcomes.keys()];
    this.outcomes.clear();
    this.requestOutcomes(client, ids);
  }

  toString() { return `EventStore(${this.label}, ${this.order.length})`; }
}

export function useEventStore(store: EventStore): number {
  return useSyncExternalStore(store.subscribe, store.getVersion, store.getVersion);
}
