/**
 * MOCK HOST — DEMO ONLY.
 *
 * A deterministic stand-in for the native JUCE host so the UI can be designed,
 * reviewed and tested in a normal browser. It never fetches wildlife sources,
 * touches SQLite, reads files or produces audio. Every event it emits has
 * provenance "demo". Meter values are simulated and flagged as such.
 *
 * Determinism: event content is a pure function of (seed, minute index), so a
 * given wall-clock minute always yields the same events. Arrival is paced by
 * the wall clock to imitate polling, which is the only non-pure input.
 */
import type {
  CoverageInterval, EventView, HostCapabilities, HostMethods, HostReply, RegionView, SessionView, SourceHealth,
} from "./contracts";
import type {
  AllMethods, AudioState, CollectionState, HostNotification, MappingRow, MethodName, MusicalOutcome, RegionActivity,
  RenderJob, VoiceDefinition,
} from "./provisional";
import type { ExtendedHost, ExtendedRequest } from "./client";
import {
  EFFECTS, REGIONS, SCALE_NOTES, SHARK_FIXES, SOURCE_IDS, SOURCE_NAMES, SPECIES, STATIONS, VOICE_PARAMETERS, initialMappings, initialVoices,
} from "./mockData";

export type MockScenario =
  | "normal"
  | "empty_no_sources"
  | "stale_observations"
  | "sources_offline"
  | "bridge_reconnecting"
  | "audio_unavailable"
  | "audio_device_lost"
  | "history_unknown"
  | "history_no_matches"
  | "history_unavailable"
  | "truncated_capture"
  | "corrupt_piece"
  | "render_failure"
  | "unsupported_capabilities";

export const MOCK_SCENARIOS: Array<{ id: MockScenario; label: string }> = [
  { id: "normal", label: "Normal demo activity" },
  { id: "empty_no_sources", label: "No sources selected (empty)" },
  { id: "stale_observations", label: "Connected, observations stale" },
  { id: "sources_offline", label: "Sources offline / retrying" },
  { id: "bridge_reconnecting", label: "Host bridge reconnecting" },
  { id: "audio_unavailable", label: "Audio unavailable" },
  { id: "audio_device_lost", label: "Audio device lost" },
  { id: "history_unknown", label: "History: coverage unknown" },
  { id: "history_no_matches", label: "History: no matching records" },
  { id: "history_unavailable", label: "History: archive unavailable" },
  { id: "truncated_capture", label: "Saved piece truncated" },
  { id: "corrupt_piece", label: "Saved piece missing / corrupt" },
  { id: "render_failure", label: "Render fails" },
  { id: "unsupported_capabilities", label: "Host lacks history/render/voice edit" },
];

const CONTRACT_METHODS: Array<keyof HostMethods> = [
  "getCapabilities", "getSourceHealth", "getChanges", "listSessions", "prepareReplay", "queryHistory", "setTransport", "getTransport",
  "setVoiceParameter", "listRegions",
];
const PROVISIONAL_METHODS: MethodName[] = [
  "getCatalog", "getMusicalOutcomes", "getRegionActivity", "setSpeciesMix", "getCollectionState", "setCollection",
  "getAudioState", "setAudioEnabled", "setMasterGain", "freezeHistorySelection", "getVoiceLibrary", "getVoice",
  "setVoiceType", "saveVoice", "revertVoice", "getMappings", "setMappingEnabled", "auditionEvent", "listEffects",
  "setRegionEffect", "setRegionPolicy", "getMaterialEvents", "startRender", "getRenderJob", "cancelRender",
];
const UNSUPPORTED_IN_SCENARIO: MethodName[] = ["queryHistory", "freezeHistorySelection", "startRender", "setVoiceParameter", "setVoiceType", "saveVoice"];

const MINUTE = 60_000;
const BASELINE_WINDOW_MS = 60 * MINUTE;
const POLL_BATCH_MS = 20_000; // demo arrivals are released in 20 s batches, like a poller

function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
function hashString(text: string): number {
  let h = 2166136261;
  for (let i = 0; i < text.length; i += 1) h = Math.imul(h ^ text.charCodeAt(i), 16777619);
  return h >>> 0;
}
const iso = (ms: number) => new Date(ms).toISOString();

function pickWeighted(rng: () => number, weights: Record<string, number>): string {
  const entries = Object.entries(weights);
  const total = entries.reduce((sum, [, w]) => sum + w, 0);
  let r = rng() * total;
  for (const [key, w] of entries) {
    r -= w;
    if (r <= 0) return key;
  }
  return entries[entries.length - 1]![0];
}

function pointInRing(lon: number, lat: number, ring: Array<[number, number]>): boolean {
  let inside = false;
  for (let i = 0, j = ring.length - 1; i < ring.length; j = i, i += 1) {
    const [xi, yi] = ring[i]!;
    const [xj, yj] = ring[j]!;
    if (yi > lat !== yj > lat && lon < ((xj - xi) * (lat - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

/** Deterministic acoustic events observed during one wall-clock minute. */
function minuteEvents(minuteIndex: number, seedLabel: string): EventView[] {
  const rng = mulberry32(hashString(`${seedLabel}:${minuteIndex}`));
  const minuteStart = minuteIndex * MINUTE;
  const nzHour = (new Date(minuteStart).getUTCHours() + 13) % 24;
  // Demo activity curve: busier at dawn, quieter (not silent) at night so the UI always has material.
  const dawn = nzHour >= 5 && nzHour <= 7 ? 2 : nzHour >= 21 || nzHour <= 3 ? 0.6 : 1;
  const count = Math.floor(rng() * 5 * dawn + 0.3);
  const events: EventView[] = [];
  for (let i = 0; i < count; i += 1) {
    const station = STATIONS[Math.floor(rng() * STATIONS.length)]!;
    const speciesId = pickWeighted(rng, station.weights);
    const species = SPECIES.find((s) => s.id === speciesId)!;
    const observed = minuteStart + Math.floor(rng() * MINUTE);
    const lag = 25_000 + Math.floor(rng() * 70_000);
    const arrival = Math.ceil((observed + lag) / POLL_BATCH_MS) * POLL_BATCH_MS;
    const confidence = Math.round((0.45 + rng() * 0.53) * 100) / 100;
    events.push({
      id: `demo-${seedLabel}-${minuteIndex}-${i}`,
      revisionSequence: `${arrival}${String(i).padStart(3, "0")}`,
      sourceId: SOURCE_IDS.birdweather,
      speciesId,
      commonName: species.commonName,
      individualId: null,
      deploymentId: null,
      stationId: station.id,
      eventKind: "acoustic_species_detection",
      locationKind: "station_position",
      position: { latitude: station.latitude, longitude: station.longitude, uncertaintyMetres: null },
      observedAtUtc: iso(observed),
      firstSeenAtUtc: iso(arrival),
      confidence: { value: confidence, meaning: "BirdWeather detection score (not comparable across providers)" },
      attribution: "Demo data — illustrative, not a BirdWeather record",
      qualityFlags: [],
      provenance: "demo",
    });
  }
  // A satellite fix roughly every 97 minutes, cycling through measured demo points.
  if (minuteIndex % 97 === 0) {
    const fix = SHARK_FIXES[Math.floor(minuteIndex / 97) % SHARK_FIXES.length]!;
    const observed = minuteStart + 5_000;
    const arrival = Math.ceil((observed + 4 * 3_600_000) / POLL_BATCH_MS) * POLL_BATCH_MS; // published hours later
    events.push({
      id: `demo-${seedLabel}-${minuteIndex}-fix`,
      revisionSequence: `${arrival}999`,
      sourceId: SOURCE_IDS.sos,
      speciesId: "white_shark",
      commonName: "White shark",
      individualId: fix.individualId,
      deploymentId: fix.deploymentId,
      stationId: null,
      eventKind: "satellite_fix",
      locationKind: "satellite_estimate",
      position: { latitude: fix.latitude, longitude: fix.longitude, uncertaintyMetres: fix.uncertaintyMetres },
      observedAtUtc: iso(observed),
      firstSeenAtUtc: iso(arrival),
      confidence: null,
      attribution: "Demo data — illustrative, not an NZ Great White record",
      qualityFlags: ["location_class_estimated"],
      provenance: "demo",
    });
  }
  return events;
}

function eventsBetween(fromMs: number, toMs: number, seedLabel: string): EventView[] {
  const out: EventView[] = [];
  for (let m = Math.floor(fromMs / MINUTE); m <= Math.floor(toMs / MINUTE); m += 1) {
    for (const e of minuteEvents(m, seedLabel)) {
      const t = Date.parse(e.observedAtUtc);
      if (t >= fromMs && t < toMs) out.push(e);
    }
  }
  return out;
}

const compareSequence = (a: string, b: string) => (a.length !== b.length ? a.length - b.length : a < b ? -1 : a > b ? 1 : 0);

interface Selection { id: string; speciesIds: string[]; regionId: string; fromMs: number; toMs: number; coverage: CoverageInterval[] }
interface Prepared { id: string; sessionId: string; durationUs: bigint; generation: bigint }

export class MockAtlasHost implements ExtendedHost {
  readonly isMock = true;
  private scenario: MockScenario = "normal";
  private listeners = new Set<(n: HostNotification) => void>();
  private voices = new Map<string, VoiceDefinition>(initialVoices().map((v) => [v.id, v]));
  private mappings = new Map<string, MappingRow[]>();
  private mix = new Map<string, { muted: boolean; soloed: boolean }>();
  private regions: RegionView[] = REGIONS.map((r) => ({
    id: r.id, name: r.name, priority: r.priority, overlapPolicy: "highest_priority",
    geometry: { type: "MultiPolygon", coordinates: [[r.ring]] },
    effectAssignments: [{ effectId: r.effect.effectId, enabled: true, send: r.effect.send }],
  }));
  private uncertainPosition: "use_reported_point" | "skip_region_effects" = "use_reported_point";
  private changeRevision = 100n;
  private transportGeneration = 1n;
  private transport: { state: "playing" | "paused" | "stopped"; positionUs: bigint; startedWallMs: number; prepared: Prepared | null } =
    { state: "stopped", positionUs: 0n, startedWallMs: 0, prepared: null };
  private transportTimer: number | null = null;
  private audio: AudioState = { status: "running", deviceName: "Built-in Output (demo)", masterGainDb: -6, peakDbfs: -14, meterIsSimulated: true, clipping: false, activeSynthVoices: 3, maxSynthVoices: 32 };
  private collection: CollectionState = { state: "collecting", listening: true, requestedDurationMinutes: 240, startedAtUtc: iso(Date.now() - 72 * MINUTE - 40_000), elapsedUs: "0", recordCount: 143, storageWarning: "none" };
  private selections = new Map<string, Selection>();
  private prepared = new Map<string, Prepared>();
  private renders = new Map<string, RenderJob>();
  private bridgeDownUntil = 0;
  private readonly sessionStartMs = Date.now();

  setScenario(scenario: MockScenario) {
    this.scenario = scenario;
    this.audio.status = scenario === "audio_unavailable" ? "unavailable" : scenario === "audio_device_lost" ? "device_lost" : "running";
    this.audio.deviceName = scenario === "audio_unavailable" ? null : "Built-in Output (demo)";
    if (scenario === "bridge_reconnecting") {
      this.bridgeDownUntil = Date.now() + 8_000;
      this.emit({ type: "connection", state: "reconnecting" });
      window.setTimeout(() => this.emit({ type: "connection", state: "connected" }), 8_000);
    }
    this.emit({ type: "sourceHealthChanged" });
    this.emit({ type: "audioStateChanged" });
  }
  getScenario() { return this.scenario; }

  /** Demo-only: emulate a host automation write to prove the UI does not echo it back. */
  simulateAutomation(voiceId: string, parameterId: string) {
    const voice = this.voices.get(voiceId);
    if (!voice) return;
    const next = Math.round((0.2 + Math.random() * 0.6) * 1000) / 1000;
    voice.values = { ...voice.values, [parameterId]: next };
    this.changeRevision += 1n;
    this.emit({ type: "voiceParameterChanged", voiceId, parameterId, normalizedValue: next, changeRevision: this.changeRevision.toString(), origin: "automation" });
  }

  subscribe(listener: (n: HostNotification) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
  private emit(n: HostNotification) {
    for (const l of this.listeners) l(n);
  }

  async request<M extends MethodName>(req: ExtendedRequest<M>, signal?: AbortSignal): Promise<HostReply<AllMethods[M]["response"]>> {
    // Simulated bridge latency keeps the UI honest about async acknowledgement.
    await new Promise((resolve) => window.setTimeout(resolve, 40 + (hashString(req.requestId) % 80)));
    if (signal?.aborted) return { requestId: req.requestId, ok: false, error: { code: "unavailable", message: "Request aborted" } };
    if (Date.now() < this.bridgeDownUntil) return { requestId: req.requestId, ok: false, error: { code: "unavailable", message: "Host bridge reconnecting" } };
    if (this.scenario === "unsupported_capabilities" && UNSUPPORTED_IN_SCENARIO.includes(req.method)) {
      return { requestId: req.requestId, ok: false, error: { code: "unsupported", message: `${req.method} is not supported by this host` } };
    }
    try {
      const result = this.handle(req.method, req.args as never);
      return { requestId: req.requestId, ok: true, result: result as AllMethods[M]["response"] };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      const code = message.startsWith("invalid:") ? "invalid_request" : message.startsWith("stale:") ? "stale_generation" : message.startsWith("unavailable:") ? "unavailable" : "internal";
      return { requestId: req.requestId, ok: false, error: { code, message: message.replace(/^\w+:\s*/, "") } };
    }
  }

  // eslint-style exhaustive dispatch; each branch validates its own input.
  private handle(method: MethodName, args: Record<string, unknown>): unknown {
    switch (method) {
      case "getCapabilities": return this.capabilities();
      case "getSourceHealth": return this.sourceHealth();
      case "getChanges": return this.changes(args as HostMethods["getChanges"]["request"]);
      case "listSessions": return this.sessions();
      case "prepareReplay": return this.prepareReplay(args as HostMethods["prepareReplay"]["request"]);
      case "queryHistory": return this.queryHistory(args as HostMethods["queryHistory"]["request"]);
      case "getTransport": this.syncPosition(); return this.transportSnapshot();
      case "setTransport": return this.setTransport(args as HostMethods["setTransport"]["request"]);
      case "setVoiceParameter": return this.setVoiceParameter(args as HostMethods["setVoiceParameter"]["request"]);
      case "listRegions": return structuredClone(this.regions);
      case "getCatalog": return this.catalog();
      case "getMusicalOutcomes": return this.outcomes((args as { eventIds: string[] }).eventIds);
      case "getRegionActivity": return this.regionActivity(args as AllMethods["getRegionActivity"]["request"]);
      case "setSpeciesMix": return this.setMix(args as AllMethods["setSpeciesMix"]["request"]);
      case "getCollectionState": return this.collectionState();
      case "setCollection": return this.setCollection(args as AllMethods["setCollection"]["request"]);
      case "getAudioState": return { ...this.audio, peakDbfs: this.audio.status === "running" ? this.audio.peakDbfs : null };
      case "setAudioEnabled": return this.setAudioEnabled((args as { enabled: boolean }).enabled);
      case "setMasterGain": return this.setMasterGain(args as AllMethods["setMasterGain"]["request"]);
      case "freezeHistorySelection": return this.freeze(args as AllMethods["freezeHistorySelection"]["request"]);
      case "getVoiceLibrary": return this.voiceLibrary();
      case "getVoice": return this.voice((args as { voiceId: string }).voiceId);
      case "setVoiceType": return this.setVoiceType(args as AllMethods["setVoiceType"]["request"]);
      case "saveVoice": return this.saveVoice(args as AllMethods["saveVoice"]["request"]);
      case "revertVoice": return this.revertVoice((args as { voiceId: string }).voiceId);
      case "getMappings": return this.mappingsFor((args as { voiceId: string }).voiceId);
      case "setMappingEnabled": return this.setMapping(args as AllMethods["setMappingEnabled"]["request"]);
      case "auditionEvent": return { accepted: this.audio.status === "running" };
      case "listEffects": return EFFECTS;
      case "setRegionEffect": return this.setRegionEffect(args as AllMethods["setRegionEffect"]["request"]);
      case "setRegionPolicy": return this.setRegionPolicy(args as AllMethods["setRegionPolicy"]["request"]);
      case "getMaterialEvents": return this.materialEvents(args as AllMethods["getMaterialEvents"]["request"]);
      case "startRender": return this.startRender(args as AllMethods["startRender"]["request"]);
      case "getRenderJob": return this.renderJob((args as { jobId: string }).jobId);
      case "cancelRender": return this.cancelRender((args as { jobId: string }).jobId);
      default: throw new Error(`invalid: unknown method ${String(method)}`);
    }
  }

  private capabilities(): HostCapabilities {
    const all = [...CONTRACT_METHODS, ...PROVISIONAL_METHODS];
    const supported = this.scenario === "unsupported_capabilities" ? all.filter((m) => !UNSUPPORTED_IN_SCENARIO.includes(m)) : all;
    // contracts.ts types supportedMethods as keyof HostMethods; provisional names are listed too (see INTEGRATION_GAPS.md).
    return { protocolVersion: 1, implementation: "mock", supportedMethods: supported as Array<keyof HostMethods>, audioAvailable: this.audio.status === "running", maxEventBatch: 200, maxVisibleMarkers: 400 };
  }

  private catalog() {
    return {
      species: SPECIES,
      stations: STATIONS.map((s) => ({ id: s.id, name: s.name, sourceId: SOURCE_IDS.birdweather, latitude: s.latitude, longitude: s.longitude, positionDescription: "Illustrative station position (demo)" })),
      sources: [
        { id: SOURCE_IDS.birdweather, name: SOURCE_NAMES.birdweather!, measurement: "Acoustic species detection at fixed stations", attribution: "Demo stand-in for BirdWeather (no real records)" },
        { id: SOURCE_IDS.sos, name: SOURCE_NAMES["nz-great-white-sos"]!, measurement: "Satellite position fix of a tagged individual", attribution: "Demo stand-in for NZ Great White app (no real records)" },
        { id: SOURCE_IDS.archive, name: SOURCE_NAMES["archive-import"]!, measurement: "Archived records (history queries)", attribution: "Demo archive" },
      ],
    };
  }

  private sourceHealth(): SourceHealth[] {
    const now = Date.now();
    if (this.scenario === "empty_no_sources") {
      return [
        { sourceId: SOURCE_IDS.birdweather, connection: "disabled", lastSuccessfulFetchUtc: null, newestObservationUtc: null, publicationLagSeconds: null, coverage: "unknown", permissionsStatus: "public_access_reuse_unresolved", errorCode: null },
        { sourceId: SOURCE_IDS.sos, connection: "disabled", lastSuccessfulFetchUtc: null, newestObservationUtc: null, publicationLagSeconds: null, coverage: "unknown", permissionsStatus: "public_access_reuse_unresolved", errorCode: null },
      ];
    }
    const offline = this.scenario === "sources_offline";
    const stale = this.scenario === "stale_observations";
    return [
      {
        sourceId: SOURCE_IDS.birdweather,
        connection: offline ? "retrying" : "connected",
        lastSuccessfulFetchUtc: iso(offline ? now - 14 * MINUTE : now - 38_000),
        newestObservationUtc: iso(stale ? now - 6 * 3_600_000 : offline ? now - 15 * MINUTE : now - 70_000),
        publicationLagSeconds: null,
        coverage: "best_effort",
        permissionsStatus: "public_access_reuse_unresolved",
        errorCode: offline ? "network_timeout" : null,
      },
      {
        sourceId: SOURCE_IDS.sos,
        connection: offline ? "offline" : "connected",
        lastSuccessfulFetchUtc: iso(offline ? now - 2 * 3_600_000 : now - 9 * MINUTE),
        newestObservationUtc: iso(now - 5 * 3_600_000),
        publicationLagSeconds: null,
        coverage: "partial",
        permissionsStatus: "public_access_reuse_unresolved",
        errorCode: offline ? "http_503" : null,
      },
    ];
  }

  private changes(req: HostMethods["getChanges"]["request"]): HostMethods["getChanges"]["response"] {
    if (!/^\d+$/.test(req.afterSequence)) throw new Error("invalid: afterSequence must be a decimal string");
    const limit = Math.min(Math.max(1, req.limit), 200);
    // "Empty" and "stale" demo scenarios present a different (quiet) store so the UI starts a fresh baseline;
    // "offline" keeps the same store: records already received stay visible, nothing new arrives.
    const quietStore = this.scenario === "empty_no_sources" || this.scenario === "stale_observations";
    const storeId = quietStore ? "demo-store-quiet" : "demo-store-1";
    if (quietStore || this.scenario === "sources_offline") {
      return { storeId, nextSequence: req.storeId === storeId ? req.afterSequence : "0", events: [] };
    }
    const now = Date.now();
    const isBaseline = req.afterSequence === "0" || req.storeId !== storeId;
    const all = eventsBetween(now - BASELINE_WINDOW_MS - 2 * MINUTE, now, "live")
      .filter((e) => Date.parse(e.firstSeenAtUtc) <= now && compareSequence(e.revisionSequence, isBaseline ? "0" : req.afterSequence) > 0)
      .sort((a, b) => compareSequence(a.revisionSequence, b.revisionSequence));
    const page = all.slice(0, limit);
    const next = page.length ? page[page.length - 1]!.revisionSequence : req.afterSequence;
    // A fresh cursor ("0") or an arrival older than this host session is history, never a new live note.
    return {
      storeId, nextSequence: next,
      events: page.map((event) => {
        const baseline = isBaseline || Date.parse(event.firstSeenAtUtc) < this.sessionStartMs;
        return { event, liveEligible: !baseline, change: baseline ? "baseline" as const : "new" as const };
      }),
    };
  }

  private sessions(): SessionView[] {
    const list: SessionView[] = [
      { id: "session-kaikoura-dawn", name: "Kaikōura dawn · 4 h", state: "frozen", origin: "collection", eventCount: 412, captureTruncated: this.scenario === "truncated_capture" },
      { id: "session-rotorua", name: "Rotorua morning", state: "interrupted", origin: "collection", eventCount: 97, captureTruncated: false },
      { id: "session-sharks-import", name: "Shark fixes (imported bundle)", state: "frozen", origin: "imported_bundle", eventCount: 18, captureTruncated: false },
    ];
    if (this.scenario === "corrupt_piece") list.push({ id: "session-corrupt", name: "Night chorus (bundle unreadable)", state: "frozen", origin: "imported_bundle", eventCount: 0, captureTruncated: false });
    return list;
  }

  private prepareReplay(req: HostMethods["prepareReplay"]["request"]): HostMethods["prepareReplay"]["response"] {
    if (req.sessionId === "session-corrupt") throw new Error("internal: Bundle failed integrity check (checksum mismatch). The piece was not opened.");
    if (!/^\d+$/.test(req.speedNumerator) || !/^\d+$/.test(req.speedDenominator) || req.speedDenominator === "0") throw new Error("invalid: speed must be a positive rational");
    const frozenSelection = req.sessionId.startsWith("frozen-") ? req.sessionId.slice("frozen-".length) : null;
    const session = this.sessions().find((s) => s.id === req.sessionId)
      ?? (frozenSelection && this.selections.has(frozenSelection) ? { id: req.sessionId, eventCount: this.selectionEvents(frozenSelection).length } : null);
    if (!session) throw new Error("invalid: unknown session");
    const sourceDurationUs = 4n * 3_600_000_000n;
    const durationUs = (sourceDurationUs * BigInt(req.speedDenominator)) / BigInt(req.speedNumerator);
    this.transportGeneration += 1n;
    const prepared: Prepared = { id: `prep-${this.transportGeneration}`, sessionId: req.sessionId, durationUs, generation: this.transportGeneration };
    this.prepared.set(prepared.id, prepared);
    this.transport = { state: "stopped", positionUs: 0n, startedWallMs: 0, prepared };
    return { preparationId: prepared.id, generation: prepared.generation.toString(), durationUs: durationUs.toString(), eventCount: session.eventCount, peakEventsPerSecond: 6.5 };
  }

  private setTransport(req: HostMethods["setTransport"]["request"]): HostMethods["setTransport"]["response"] {
    if (req.action === "panic") {
      this.audio.activeSynthVoices = 0;
      this.emit({ type: "audioStateChanged" });
      return this.transportSnapshot();
    }
    const prepared = req.preparationId ? this.prepared.get(req.preparationId) : this.transport.prepared;
    if (!prepared) throw new Error("unavailable: nothing prepared for playback");
    if (prepared.generation !== this.transportGeneration) throw new Error("stale: preparation superseded by a newer generation");
    this.syncPosition();
    if (req.action === "play") { this.transport.state = "playing"; this.transport.startedWallMs = Date.now(); this.startTicker(); }
    if (req.action === "pause") { this.transport.state = "paused"; this.stopTicker(); }
    if (req.action === "stop") { this.transport.state = "stopped"; this.transport.positionUs = 0n; this.stopTicker(); }
    if (req.action === "seek") {
      if (!req.positionUs || !/^\d+$/.test(req.positionUs)) throw new Error("invalid: seek requires positionUs");
      const pos = BigInt(req.positionUs);
      this.transport.positionUs = pos > prepared.durationUs ? prepared.durationUs : pos;
      this.transport.startedWallMs = Date.now();
    }
    this.emitTransport();
    return this.transportSnapshot();
  }
  private transportSnapshot(): HostMethods["getTransport"]["response"] {
    return { preparationId: this.transport.prepared?.id ?? null, generation: this.transportGeneration.toString(), state: this.transport.state,
      positionUs: this.transport.positionUs.toString(), durationUs: (this.transport.prepared?.durationUs ?? 0n).toString() };
  }
  private syncPosition() {
    if (this.transport.state !== "playing") return;
    const now = Date.now();
    this.transport.positionUs += BigInt(Math.round((now - this.transport.startedWallMs) * 1000));
    this.transport.startedWallMs = now;
    const end = this.transport.prepared?.durationUs ?? 0n;
    if (this.transport.positionUs >= end) { this.transport.positionUs = end; this.transport.state = "stopped"; this.stopTicker(); }
  }
  private emitTransport() {
    this.emit({ type: "transportChanged", generation: this.transportGeneration.toString(), state: this.transport.state, positionUs: this.transport.positionUs.toString() });
  }
  // Simulates the native host pushing position updates; the UI never runs its own transport clock.
  private startTicker() {
    this.stopTicker();
    this.transportTimer = window.setInterval(() => { this.syncPosition(); this.emitTransport(); }, 250);
  }
  private stopTicker() {
    if (this.transportTimer !== null) window.clearInterval(this.transportTimer);
    this.transportTimer = null;
  }

  private queryHistory(req: HostMethods["queryHistory"]["request"]): HostMethods["queryHistory"]["response"] {
    const fromMs = Date.parse(req.fromUtc);
    const toMs = Date.parse(req.toUtc);
    if (!Number.isFinite(fromMs) || !Number.isFinite(toMs) || toMs <= fromMs) throw new Error("invalid: fromUtc must be before toUtc");
    if (toMs - fromMs > 24 * 3_600_000) throw new Error("invalid: demo archive limits a query to 24 hours");
    if (!req.speciesIds.length) throw new Error("invalid: choose at least one species");
    if (this.scenario === "history_unavailable") return { selectionId: "", eventCount: null, coverage: [], status: "unavailable" };
    const id = `sel-${hashString(JSON.stringify(req)).toString(16)}`;
    const span = toMs - fromMs;
    const at = (f: number) => iso(fromMs + Math.round(span * f));
    let coverage: CoverageInterval[];
    let status: "ready" | "partial" | "unknown";
    if (this.scenario === "history_unknown") {
      coverage = [{ fromUtc: req.fromUtc, toUtc: req.toUtc, state: "unknown", sourceIds: [], resolutionDescription: null }];
      status = "unknown";
    } else if (this.scenario === "history_no_matches") {
      coverage = [{ fromUtc: req.fromUtc, toUtc: req.toUtc, state: "confirmed_coverage_no_detections", sourceIds: [SOURCE_IDS.archive], resolutionDescription: "As recorded" }];
      status = "ready";
    } else {
      const parts: Array<[number, number, CoverageInterval["state"]]> = [[0, 0.228, "records_available"], [0.228, 0.35, "confirmed_coverage_no_detections"], [0.35, 0.733, "records_available"], [0.733, 0.8, "missing_coverage"], [0.8, 0.956, "records_available"], [0.956, 1, "unknown"]];
      coverage = parts.map(([a, b, state]) => ({ fromUtc: at(a), toUtc: at(b), state, sourceIds: state === "unknown" ? [] : [SOURCE_IDS.archive], resolutionDescription: state === "records_available" ? "As recorded (per detection)" : null }));
      status = "partial";
    }
    this.selections.set(id, { id, speciesIds: req.speciesIds, regionId: req.regionId, fromMs, toMs, coverage });
    const eventCount = status === "unknown" ? null : this.selectionEvents(id).length;
    return { selectionId: id, eventCount, coverage, status };
  }

  private selectionEvents(selectionId: string): EventView[] {
    const sel = this.selections.get(selectionId);
    if (!sel || this.scenario === "history_no_matches" || this.scenario === "history_unknown") return [];
    const region = this.regions.find((r) => r.id === sel.regionId);
    const ranges = sel.coverage.filter((c) => c.state === "records_available").map((c) => [Date.parse(c.fromUtc), Date.parse(c.toUtc)] as const);
    return eventsBetween(sel.fromMs, sel.toMs, "archive")
      .filter((e) => e.speciesId && sel.speciesIds.includes(e.speciesId))
      .filter((e) => !region || !e.position || region.geometry.coordinates.some((poly) => pointInRing(e.position!.longitude, e.position!.latitude, poly[0] as Array<[number, number]>)) || sel.regionId === "region-all")
      .filter((e) => { const t = Date.parse(e.observedAtUtc); return ranges.some(([a, b]) => t >= a && t < b); })
      .map((e) => ({ ...e, sourceId: SOURCE_IDS.archive, attribution: "Demo archive — illustrative" }));
  }

  private materialEvents(req: AllMethods["getMaterialEvents"]["request"]) {
    let events: EventView[];
    if (req.material.kind === "selection") events = this.selectionEvents(req.material.selectionId);
    else {
      const start = this.sessionStartMs - 5 * 3_600_000;
      events = eventsBetween(start, start + 4 * 3_600_000, req.material.sessionId).map((e) => ({ ...e, provenance: req.material.kind === "session" && req.material.sessionId === "session-sharks-import" ? "imported" as const : "demo" as const }));
    }
    return { total: events.length, events: events.slice(req.offset, req.offset + Math.min(req.limit, 100_000)) };
  }

  private freeze(req: AllMethods["freezeHistorySelection"]["request"]) {
    const sel = this.selections.get(req.selectionId);
    if (!sel) throw new Error("invalid: unknown or expired selection");
    if (!req.name.trim()) throw new Error("invalid: name required");
    const records = sel.coverage.filter((c) => c.state === "records_available");
    return {
      id: `frozen-${sel.id}`, name: req.name.trim(), state: "frozen" as const, origin: "archive_snapshot" as const,
      eventCount: this.selectionEvents(sel.id).length, captureTruncated: false,
      requestedFromUtc: iso(sel.fromMs), requestedToUtc: iso(sel.toMs),
      materialFromUtc: records[0]?.fromUtc ?? null, materialToUtc: records[records.length - 1]?.toUtc ?? null,
    };
  }

  private mixFor(speciesId: string) { return this.mix.get(speciesId) ?? { muted: false, soloed: false }; }
  private anySolo() { return [...this.mix.values()].some((m) => m.soloed); }
  private voiceIdForSpecies(speciesId: string): string | null {
    const v = [...this.voices.values()].find((x) => x.scope === "species" && x.targetId === speciesId);
    if (v) return v.id;
    const species = SPECIES.find((s) => s.id === speciesId);
    if (speciesId === "kereru" || !species) return null;
    return [...this.voices.values()].find((x) => x.scope === "group" && x.targetId === species.group)?.id ?? null;
  }

  private outcomes(eventIds: string[]): Record<string, MusicalOutcome> {
    if (eventIds.length > 500) throw new Error("invalid: at most 500 ids per request");
    const result: Record<string, MusicalOutcome> = {};
    for (const id of eventIds) {
      const h = hashString(id);
      const speciesId = id.includes("-fix") ? "white_shark" : this.speciesFromId(id);
      if (!speciesId) { result[id] = { kind: "not_evaluated" }; continue; }
      const mix = this.mixFor(speciesId);
      const voiceId = this.voiceIdForSpecies(speciesId);
      if (mix.muted) result[id] = { kind: "filtered", reason: "species_muted" };
      else if (this.anySolo() && !mix.soloed) result[id] = { kind: "filtered", reason: "not_soloed" };
      else if (!voiceId) result[id] = { kind: "filtered", reason: "no_voice_assigned" };
      else if (h % 9 === 0) result[id] = { kind: "filtered", reason: "confidence_below_threshold" };
      else if (h % 5 === 0) result[id] = { kind: "suppressed", reason: "density_cap" };
      else if (h % 7 === 0) result[id] = { kind: "grouped", groupId: `grp-${h % 997}`, groupSize: 2 + (h % 4), voiceId };
      else {
        const region = REGIONS[h % 3 === 0 ? 1 : 0]!;
        result[id] = { kind: "sounded", voiceId, noteName: SCALE_NOTES[h % SCALE_NOTES.length]!, velocity: 56 + (h % 40), pan: ((h % 41) - 20) / 20, regionSends: h % 4 === 0 ? [] : [{ regionId: region.id, effectId: region.effect.effectId, send: region.effect.send }] };
      }
    }
    return result;
  }
  private speciesCache = new Map<string, string | null>();
  private speciesFromId(id: string): string | null {
    if (this.speciesCache.has(id)) return this.speciesCache.get(id)!;
    const match = /^demo-(.+)-(\d+)-(\d+)$/.exec(id);
    let speciesId: string | null = null;
    if (match) {
      const e = minuteEvents(Number(match[2]), match[1]!).find((x) => x.id === id);
      speciesId = e?.speciesId ?? null;
    }
    this.speciesCache.set(id, speciesId);
    return speciesId;
  }

  private regionActivity(req: AllMethods["getRegionActivity"]["request"]): RegionActivity {
    const region = this.regions.find((r) => r.id === req.regionId);
    if (!region) throw new Error("invalid: unknown region");
    if (req.windowMinutes < 1 || req.windowMinutes > 24 * 60) throw new Error("invalid: windowMinutes out of range");
    const now = Date.now();
    const from = now - req.windowMinutes * MINUTE;
    const quiet = this.scenario === "empty_no_sources" || this.scenario === "sources_offline" || this.scenario === "stale_observations";
    const inRegion = (e: EventView) => !!e.position && pointInRing(e.position.longitude, e.position.latitude, region.geometry.coordinates[0]![0] as Array<[number, number]>);
    const events = req.material
      ? this.materialEvents({ material: req.material, offset: 0, limit: 100_000 }).events.filter(inRegion)
      : quiet ? [] : eventsBetween(from, now, "live").filter((e) => {
        const t = Date.parse(req.basis === "observed" ? e.observedAtUtc : e.firstSeenAtUtc);
        return t >= from && t <= now && Date.parse(e.firstSeenAtUtc) <= now && inRegion(e);
      });
    const counts = new Map<string, number>();
    for (const e of events) if (e.speciesId) counts.set(e.speciesId, (counts.get(e.speciesId) ?? 0) + 1);
    const total = events.length;
    const outcomes = this.outcomes(events.map((e) => e.id));
    const values = Object.values(outcomes);
    const speciesIds = SPECIES.filter((s) => s.group === "forest_bird").map((s) => s.id);
    return {
      regionId: region.id, windowFromUtc: iso(from), windowToUtc: iso(now), basis: req.basis,
      species: speciesIds.map((id) => {
        const voiceId = this.voiceIdForSpecies(id);
        const def = voiceId ? this.voices.get(voiceId) : undefined;
        const voiceLabel = def ? (def.scope === "group" ? "group default" : def.name.replace(/^Voice /, "")) : null;
        return { speciesId: id, detections: counts.get(id) ?? 0, share: total ? (counts.get(id) ?? 0) / total : 0, voiceId, voiceLabel, ...this.mixFor(id) };
      }).sort((a, b) => b.detections - a.detections),
      totals: {
        records: total,
        eligible: values.filter((o) => o.kind !== "filtered").length,
        sounded: values.filter((o) => o.kind === "sounded").length,
        grouped: values.filter((o) => o.kind === "grouped").length,
        suppressed: values.filter((o) => o.kind === "suppressed").length,
      },
    };
  }

  private setMix(req: AllMethods["setSpeciesMix"]["request"]) {
    if (!SPECIES.some((s) => s.id === req.speciesId)) throw new Error("invalid: unknown species");
    const current = this.mixFor(req.speciesId);
    const next = { muted: req.muted ?? current.muted, soloed: req.soloed ?? current.soloed };
    this.mix.set(req.speciesId, next);
    this.changeRevision += 1n;
    return { speciesId: req.speciesId, ...next, changeRevision: this.changeRevision.toString() };
  }

  private collectionState(): CollectionState {
    const started = this.collection.startedAtUtc ? Date.parse(this.collection.startedAtUtc) : null;
    const elapsedMs = this.collection.state === "collecting" && started ? Date.now() - started : 0;
    const limitMs = (this.collection.requestedDurationMinutes ?? 0) * MINUTE;
    if (this.collection.state === "collecting" && limitMs && elapsedMs >= limitMs) this.collection.state = "complete";
    return { ...this.collection, elapsedUs: String(BigInt(Math.max(0, Math.min(elapsedMs, limitMs || elapsedMs))) * 1000n), recordCount: this.collection.state === "idle" ? 0 : 143 + Math.floor(elapsedMs / 30_000) % 400, storageWarning: this.scenario === "truncated_capture" ? "truncated" : "none" };
  }
  private setCollection(req: AllMethods["setCollection"]["request"]) {
    if (req.action === "start") {
      if (this.collection.state === "collecting") throw new Error("invalid: already collecting");
      const minutes = req.durationMinutes ?? 240;
      if (minutes < 1 || minutes > 24 * 60) throw new Error("invalid: duration out of range");
      this.collection = { state: "collecting", listening: req.listening ?? true, requestedDurationMinutes: minutes, startedAtUtc: iso(Date.now()), elapsedUs: "0", recordCount: 0, storageWarning: "none" };
    } else {
      this.collection = { ...this.collection, state: "idle" };
    }
    this.emit({ type: "collectionChanged" });
    return this.collectionState();
  }

  private setAudioEnabled(enabled: boolean): AudioState {
    if (this.audio.status === "unavailable" || this.audio.status === "device_lost") throw new Error("unavailable: no output device");
    this.audio = { ...this.audio, status: enabled ? "running" : "disabled", activeSynthVoices: enabled ? this.audio.activeSynthVoices : 0 };
    this.emit({ type: "audioStateChanged" });
    return { ...this.audio };
  }
  private setMasterGain(req: AllMethods["setMasterGain"]["request"]) {
    if (!Number.isFinite(req.gainDb)) throw new Error("invalid: gain must be finite");
    this.audio.masterGainDb = Math.min(6, Math.max(-60, req.gainDb));
    this.changeRevision += 1n;
    return { gainDb: this.audio.masterGainDb, changeRevision: this.changeRevision.toString() };
  }

  private voiceLibrary() {
    const species = (group: "forest_bird" | "shark") => SPECIES.filter((s) => s.group === group && s.id !== "kereru").map((s) => ({
      speciesId: s.id, commonName: s.commonName, scientificName: s.scientificName,
      voiceId: [...this.voices.values()].find((v) => v.scope === "species" && v.targetId === s.id)?.id ?? null,
      individuals: s.id === "white_shark" ? [
        { individualId: "WS-07", label: "WS-07 (tag)", voiceId: "voice-ws07" },
        { individualId: "WS-12", label: "WS-12 (tag)", voiceId: null },
      ] : [],
    }));
    const kereru = SPECIES.find((s) => s.id === "kereru")!;
    return {
      entries: [
        { groupId: "forest_bird", groupName: "Forest birds", groupVoiceId: "voice-group-forest", species: species("forest_bird") },
        { groupId: "shark", groupName: "Sharks", groupVoiceId: "voice-group-sharks", species: species("shark") },
        { groupId: "unassigned", groupName: "Unassigned", groupVoiceId: null, species: [{ speciesId: kereru.id, commonName: kereru.commonName, scientificName: kereru.scientificName, voiceId: null, individuals: [] }] },
      ],
      parameters: VOICE_PARAMETERS,
    };
  }
  private voice(voiceId: string): VoiceDefinition {
    const v = this.voices.get(voiceId);
    if (!v) throw new Error("invalid: unknown voice");
    return structuredClone(v);
  }
  private setVoiceType(req: AllMethods["setVoiceType"]["request"]) {
    const v = this.voices.get(req.voiceId);
    if (!v) throw new Error("invalid: unknown voice");
    v.type = req.type;
    v.dirty = true;
    return structuredClone(v);
  }
  private savedCopies = new Map<string, VoiceDefinition>(initialVoices().map((v) => [v.id, v]));
  private setVoiceParameter(req: HostMethods["setVoiceParameter"]["request"]): HostMethods["setVoiceParameter"]["response"] {
    const v = this.voices.get(req.voiceId);
    if (!v) throw new Error("invalid: unknown voice");
    if (!VOICE_PARAMETERS.some((p) => p.id === req.parameterId)) throw new Error("invalid: unknown parameter");
    if (!Number.isFinite(req.normalizedValue)) throw new Error("invalid: value must be finite");
    const value = Math.min(1, Math.max(0, req.normalizedValue));
    v.values = { ...v.values, [req.parameterId]: value };
    if (!v.overriddenParameterIds.includes(req.parameterId) && v.inheritsFromId) v.overriddenParameterIds = [...v.overriddenParameterIds, req.parameterId];
    v.dirty = true;
    this.changeRevision += 1n;
    return { normalizedValue: value, changeRevision: this.changeRevision.toString() };
  }
  private saveVoice(req: AllMethods["saveVoice"]["request"]) {
    const v = this.voices.get(req.voiceId);
    if (!v) throw new Error("invalid: unknown voice");
    if (v.savedRevision !== req.expectedRevision) throw new Error("stale: voice was saved elsewhere; reload before saving");
    v.savedRevision = String(BigInt(v.savedRevision) + 1n);
    v.dirty = false;
    this.savedCopies.set(v.id, structuredClone(v));
    return structuredClone(v);
  }
  private revertVoice(voiceId: string) {
    const saved = this.savedCopies.get(voiceId);
    if (!saved) throw new Error("invalid: unknown voice");
    this.voices.set(voiceId, structuredClone(saved));
    return structuredClone(saved);
  }
  private mappingsFor(voiceId: string): MappingRow[] {
    if (!this.voices.has(voiceId)) throw new Error("invalid: unknown voice");
    if (!this.mappings.has(voiceId)) this.mappings.set(voiceId, initialMappings());
    return structuredClone(this.mappings.get(voiceId)!);
  }
  private setMapping(req: AllMethods["setMappingEnabled"]["request"]) {
    const rows = this.mappingsFor(req.voiceId);
    const row = rows.find((r) => r.id === req.mappingId);
    if (!row) throw new Error("invalid: unknown mapping");
    row.enabled = req.enabled;
    this.mappings.set(req.voiceId, rows);
    const v = this.voices.get(req.voiceId);
    if (v) v.dirty = true;
    return row;
  }
  private setRegionEffect(req: AllMethods["setRegionEffect"]["request"]): RegionView {
    const region = this.regions.find((r) => r.id === req.regionId);
    if (!region) throw new Error("invalid: unknown region");
    const assignment = region.effectAssignments.find((a) => a.effectId === req.effectId);
    if (!assignment) throw new Error("invalid: effect not assigned to region");
    if (req.enabled !== undefined) assignment.enabled = req.enabled;
    if (req.send !== undefined) {
      if (!Number.isFinite(req.send)) throw new Error("invalid: send must be finite");
      assignment.send = Math.min(1, Math.max(0, req.send));
    }
    return structuredClone(region);
  }
  private setRegionPolicy(req: AllMethods["setRegionPolicy"]["request"]) {
    if (req.overlapPolicy) for (const r of this.regions) r.overlapPolicy = req.overlapPolicy;
    if (req.uncertainPosition) this.uncertainPosition = req.uncertainPosition;
    return { overlapPolicy: this.regions[0]!.overlapPolicy, uncertainPosition: this.uncertainPosition };
  }

  private startRender(req: AllMethods["startRender"]["request"]): RenderJob {
    if (req.sessionId === "session-corrupt") throw new Error("internal: piece cannot be read");
    const job: RenderJob = { jobId: `render-${Date.now()}`, state: "rendering", progress: 0, message: null };
    this.renders.set(job.jobId, job);
    const timer = window.setInterval(() => {
      const current = this.renders.get(job.jobId);
      if (!current || current.state !== "rendering") { window.clearInterval(timer); return; }
      const progress = Math.min(1, current.progress + 0.08);
      const failed = this.scenario === "render_failure" && progress >= 0.4;
      const next: RenderJob = failed
        ? { ...current, state: "failed", message: "Demo failure: output folder not writable (simulated)" }
        : { ...current, progress, state: progress >= 1 ? "complete" : "rendering", message: progress >= 1 ? "Rendered (demo — no file written)" : null };
      this.renders.set(job.jobId, next);
      this.emit({ type: "renderProgress", job: next });
      if (next.state !== "rendering") window.clearInterval(timer);
    }, 300);
    return job;
  }
  private renderJob(jobId: string): RenderJob {
    const job = this.renders.get(jobId);
    if (!job) throw new Error("invalid: unknown render job");
    return { ...job };
  }
  private cancelRender(jobId: string): RenderJob {
    const job = this.renders.get(jobId);
    if (!job) throw new Error("invalid: unknown render job");
    const next: RenderJob = job.state === "rendering" ? { ...job, state: "cancelled", message: "Cancelled — partial output discarded" } : job;
    this.renders.set(jobId, next);
    this.emit({ type: "renderProgress", job: next });
    return { ...next };
  }
}
