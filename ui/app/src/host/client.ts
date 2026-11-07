/**
 * The single injected host boundary used by every component.
 *
 * - Wraps an `AtlasHost` (contracts.ts) or an extended host that also answers
 *   the provisional methods in provisional.ts.
 * - Checks `getCapabilities().supportedMethods` before sending anything, so a
 *   host that lacks a method yields `{ code: "unsupported" }` locally.
 * - Adds request ids, timeouts and AbortSignal support. It never retries
 *   mutations on its own; callers decide.
 */
import { validAudioDeviceCatalog } from "./audioDevices.ts";
import { validSpatialSettings } from "./spatialSettings.ts";
import { EditBarrier } from "../app/editBarrier.ts";
import type { AtlasHost, HostCapabilities, HostReply } from "./contracts";
import type { AllMethods, HostNotification, MethodName } from "./provisional";

export interface ExtendedRequest<M extends MethodName> {
  protocolVersion: 1;
  requestId: string;
  method: M;
  args: AllMethods[M]["request"];
}

/** A host that may also implement provisional methods and push notifications. */
export interface ExtendedHost {
  request<M extends MethodName>(request: ExtendedRequest<M>, signal?: AbortSignal): Promise<HostReply<AllMethods[M]["response"]>>;
  subscribe?(listener: (notification: HostNotification) => void): () => void;
}

export type HostErrorCode = "unsupported" | "invalid_request" | "unavailable" | "stale_generation" | "internal" | "timeout" | "aborted";
export type HostResult<T> = { ok: true; value: T } | { ok: false; error: { code: HostErrorCode; message: string } };

const unsignedDecimal = (value: unknown): value is string => typeof value === "string" && /^(0|[1-9][0-9]{0,18})$/.test(value) && BigInt(value) <= 9223372036854775807n;
const identity = (value: unknown): value is string => typeof value === "string" && value.length > 0 && value.length <= 256;
const voiceIdentity = (value: unknown): value is string => typeof value === "string" && value.length > 0 && value.length <= 264;
function validBaselineTotal(method: string, value: unknown): boolean {
  if (method !== "getChanges") return true;
  if (!value || typeof value !== "object") return false;
  const reply = value as Record<string, unknown>;
  return reply.baselineTotal === undefined ||
    Number.isSafeInteger(reply.baselineTotal) && (reply.baselineTotal as number) >= 0 &&
    Array.isArray(reply.events) && (reply.baselineTotal as number) >= reply.events.length &&
    reply.events.every(row => row && row.change === "baseline" && row.liveEligible === false);
}
function validLiveActivityReply(method: string, value: unknown, args: unknown): boolean {
  if (method !== "getLiveActivity") return true;
  if (!value || typeof value !== "object") return false;
  const page = value as Record<string, unknown>;
  const request = args as {sessionId: string; afterSequence?: string; limit?: number};
  if (typeof page.sessionId !== "string" || page.sessionId.length > 256 || typeof page.sessionActive !== "boolean" ||
      page.sessionActive && page.sessionId !== request.sessionId || !unsignedDecimal(page.clockUs) || !unsignedDecimal(page.nextSequence) ||
      page.hasMore === true && page.reset === false && page.nextSequence === (request.afterSequence ?? "0") ||
      typeof page.hasMore !== "boolean" || typeof page.reset !== "boolean" || !Number.isSafeInteger(page.dropped) || (page.dropped as number) < 0 ||
      !Array.isArray(page.voices) || page.voices.length > Math.min(request.limit ?? 250, 250) ||
      !page.sessionActive && (page.voices.length > 0 || page.hasMore) ||
      !page.reset && BigInt(page.nextSequence as string) < BigInt(request.afterSequence ?? "0")) return false;
  const text = (v: unknown, max: number) => typeof v === "string" && v.length <= max;
  const nullable = (v: unknown, max: number) => v === null || text(v, max);
  return page.voices.every(voice => {
    if (!voice || !identity(voice.token) || !text(voice.eventId, 2048) || !voice.eventId || !nullable(voice.speciesId, 512) ||
        !text(voice.commonName, 2048) || !text(voice.voiceName, 2048) ||
        !["struck", "whistle", "reed", "bell", "air", "pulse", "pluck", "resonance"].includes(voice.voiceType) ||
        !nullable(voice.stationId, 2048) || !text(voice.eventKind, 128) || !text(voice.locationKind, 128) ||
        !text(voice.locationLabel, 2048) || !nullable(voice.region, 2048) || !unsignedDecimal(voice.onsetUs) ||
        !(voice.endUs === null || unsignedDecimal(voice.endUs) && BigInt(voice.endUs) >= BigInt(voice.onsetUs)) ||
        !(voice.releaseUs === undefined || unsignedDecimal(voice.releaseUs) && BigInt(voice.releaseUs) >= BigInt(voice.onsetUs))) return false;
    const p = voice.position;
    return p === null || p && typeof p.latitude === "number" && Number.isFinite(p.latitude) && p.latitude >= -90 && p.latitude <= 90 &&
      typeof p.longitude === "number" && Number.isFinite(p.longitude) && p.longitude >= -180 && p.longitude <= 180 &&
      (p.uncertaintyMetres === undefined || p.uncertaintyMetres === null || typeof p.uncertaintyMetres === "number" && Number.isFinite(p.uncertaintyMetres) && p.uncertaintyMetres >= 0);
  });
}
function validLiveSessionReply(method: string, value: unknown, args: unknown): boolean {
  if (!["getLiveChanges", "getLiveSpeciesCounts"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const reply = value as Record<string, unknown>;
  const request = args as {sessionId: string; limit?: number};
  if (typeof reply.sessionActive !== "boolean" || typeof reply.sessionId !== "string" || reply.sessionId.length > 256 ||
      reply.sessionActive && (!identity(reply.sessionId) || reply.sessionId !== request.sessionId)) return false;
  if (method === "getLiveChanges") {
    if (!identity(reply.storeId) || !unsignedDecimal(reply.nextSequence) || !Array.isArray(reply.events) ||
        reply.events.length > (request.limit ?? 200) || typeof reply.hasMore !== "boolean" ||
        !Array.isArray(reply.removedEventIds) || reply.removedEventIds.length > (request.limit ?? 200) ||
        !reply.removedEventIds.every(id => typeof id === "string" && id.length > 0 && id.length <= 2048) ||
        !reply.sessionActive && (reply.events.length > 0 || reply.removedEventIds.length > 0 || reply.hasMore)) return false;
    return reply.events.every(row => row && typeof row.liveEligible === "boolean" && ["baseline", "new", "corrected"].includes(row.change) &&
      row.event && typeof row.event.id === "string" && row.event.id.length > 0 && row.event.id.length <= 2048 &&
      unsignedDecimal(row.event.revisionSequence) && typeof row.event.observedAtUtc === "string" &&
      Number.isFinite(Date.parse(row.event.observedAtUtc)) && row.event.provenance === "live_source");
  }
  if (!unsignedDecimal(reply.revision) || !Array.isArray(reply.species) || reply.species.length > Math.min(request.limit ?? 250, 250) ||
      !(reply.nextCursor === null || typeof reply.nextCursor === "string" && reply.nextCursor.length > 0 && reply.nextCursor.length <= 2048) ||
      !reply.sessionActive && (reply.species.length > 0 || reply.nextCursor !== null) || reply.nextCursor !== null && !reply.species.length) return false;
  const ids = new Set();
  return reply.species.every(row => row && typeof row.speciesId === "string" && row.speciesId.length > 0 && row.speciesId.length <= 512 &&
    !ids.has(row.speciesId) && ids.add(row.speciesId) && typeof row.commonName === "string" && row.commonName.length <= 2048 &&
    Number.isSafeInteger(row.count) && row.count > 0);
}
function validVoiceDefinition(value: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const voice = value as Record<string, unknown>;
  return voiceIdentity(voice.id) && typeof voice.name === "string" && voice.name.length <= 512 &&
    ["group", "species", "individual"].includes(voice.scope as string) && identity(voice.targetId) &&
    (voice.inheritsFromId === null || voiceIdentity(voice.inheritsFromId)) &&
    ["struck", "whistle", "reed", "bell", "air", "pulse", "pluck", "resonance"].includes(voice.type as string) &&
    unsignedDecimal(voice.savedRevision) && unsignedDecimal(voice.changeRevision) && typeof voice.dirty === "boolean" &&
    !!voice.values && typeof voice.values === "object" && !Array.isArray(voice.values) &&
    Object.entries(voice.values).length <= 32 && Object.entries(voice.values).every(([key, value]) => identity(key) && typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1) &&
    Array.isArray(voice.overriddenParameterIds) && voice.overriddenParameterIds.length <= 32 && voice.overriddenParameterIds.every(identity);
}
function validVoiceReply(method: string, value: unknown, args: unknown): boolean {
  if (!["getVoiceLibrary", "getVoice", "setVoiceParameter", "saveVoice", "revertVoice", "setVoiceType", "applyVoicePreset", "listVoicePresets"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const reply = value as Record<string, unknown>;
  if (["getVoice", "saveVoice", "revertVoice", "setVoiceType", "applyVoicePreset"].includes(method)) return validVoiceDefinition(value) && reply.id === (args as { voiceId: string }).voiceId;
  if (method === "listVoicePresets") return reply.listeningApproved === false && Array.isArray(reply.items) && reply.items.length <= 100 && reply.items.every((preset: Record<string, unknown>) =>
    preset && identity(preset.id) && typeof preset.name === "string" && (preset.speciesId === null || identity(preset.speciesId)) &&
    typeof preset.scientificName === "string" && typeof preset.description === "string" && preset.description.length <= 4096 &&
    ["research_candidate", "ecological_metaphor", "unresolved_taxon"].includes(preset.evidenceStatus as string) && typeof preset.researchPath === "string");
  if (method === "setVoiceParameter") return typeof reply.normalizedValue === "number" && Number.isFinite(reply.normalizedValue) &&
    reply.normalizedValue >= 0 && reply.normalizedValue <= 1 && unsignedDecimal(reply.changeRevision) && typeof reply.dirty === "boolean";
  if (!Array.isArray(reply.parameters) || reply.parameters.length > 32 || !Array.isArray(reply.entries) || reply.entries.length > 513) return false;
  const parameterIds = new Set<string>();
  if (!reply.parameters.every((parameter: Record<string, unknown>) => {
    if (!parameter || !identity(parameter.id) || parameterIds.has(parameter.id)) return false;
    parameterIds.add(parameter.id);
    return typeof parameter.label === "string" && ["st", "s", "ratio", "dB", "pan", "%", "Hz"].includes(parameter.unit as string) &&
      [parameter.min, parameter.max, parameter.defaultValue].every((number) => typeof number === "number" && Number.isFinite(number)) &&
      (parameter.min as number) < (parameter.max as number) && (parameter.defaultValue as number) >= (parameter.min as number) && (parameter.defaultValue as number) <= (parameter.max as number) &&
      ["linear", "log"].includes(parameter.curve as string) && (parameter.curve !== "log" || (parameter.min as number) > 0) &&
      typeof parameter.proposal === "boolean" && (parameter.step === undefined || typeof parameter.step === "number" && Number.isFinite(parameter.step) && parameter.step > 0);
  })) return false;
  let speciesCount = 0;
  if (!reply.entries.every((entry: Record<string, unknown>) => {
    if (!entry || !identity(entry.groupId) || typeof entry.groupName !== "string" || !(entry.groupVoiceId === null || voiceIdentity(entry.groupVoiceId)) || !Array.isArray(entry.species)) return false;
    speciesCount += entry.species.length;
    return speciesCount <= 512 && entry.species.every((species: Record<string, unknown>) => species && identity(species.speciesId) &&
      typeof species.commonName === "string" && typeof species.scientificName === "string" && (species.voiceId === null || voiceIdentity(species.voiceId)) &&
      Array.isArray(species.individuals) && species.individuals.length <= 512 && species.individuals.every((individual: Record<string, unknown>) =>
        individual && identity(individual.individualId) && typeof individual.label === "string" && (individual.voiceId === null || voiceIdentity(individual.voiceId))));
  })) return false;
  return (reply.types === undefined || Array.isArray(reply.types) && reply.types.length <= 8 && reply.types.every((type: Record<string, unknown>) =>
    type && ["struck", "whistle", "reed", "bell", "air", "pulse", "pluck", "resonance"].includes(type.id as string) && typeof type.label === "string" && typeof type.proposal === "boolean")) &&
    (reply.maxDefinitions === undefined || Number.isInteger(reply.maxDefinitions) && (reply.maxDefinitions as number) > 0 && (reply.maxDefinitions as number) <= 513) &&
    (reply.editingBehavior === undefined || reply.editingBehavior === "new_notes");
}
function validLiveListeningReply(method: string, value: unknown): boolean {
  if (method === "getLiveOutcome") {
    if (!value || typeof value !== "object") return false;
    const outcome = (value as {outcome?: unknown}).outcome;
    if (outcome === null) return true;
    if (!outcome || typeof outcome !== "object") return false;
    const o = outcome as Record<string, unknown>;
    const boundedId = (v: unknown, max: number, allowEmpty = false) => typeof v === "string" && (allowEmpty || v.length > 0) && v.length <= max;
    return boundedId(o.eventId, 2048) && (o.speciesId === null || boundedId(o.speciesId, 512, true)) &&
      (o.voiceCharacter === undefined || o.voiceCharacter === null || ["struck", "whistle", "reed", "bell", "air", "pulse"].includes(o.voiceCharacter as string)) &&
      (o.region === null || typeof o.region === "string" && o.region.length <= 256) &&
      ["queued", "sounded", "grouped", "skipped"].includes(o.status as string) &&
      (o.representativeId === null || boundedId(o.representativeId, 2048));
  }
  if (!["getLiveState", "setLiveEnabled", "setLiveSettings"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const state = value as Record<string, unknown>;
  return typeof state.enabled === "boolean" && ["off", "listening", "error"].includes(state.status as string) &&
    [state.received, state.admitted, state.dropped, state.skippedRevisions].every(count => Number.isSafeInteger(count) && (count as number) >= 0) &&
    (state.mode === undefined || state.mode === "direct" || state.mode === "musical" || state.mode === "smoothed") &&
    (state.density === undefined || Number.isInteger(state.density) && (state.density as number) >= 1 && (state.density as number) <= 8) &&
    (state.pending === undefined || Number.isSafeInteger(state.pending) && (state.pending as number) >= 0) &&
    (state.catchingUp === undefined || typeof state.catchingUp === "boolean") &&
    (state.grouped === undefined || Number.isSafeInteger(state.grouped) && (state.grouped as number) >= 0) &&
    (state.composition === undefined || validCompositionReply("getComposition", { settings: state.composition, preparation: null })) &&
    (state.spatial === undefined || validSpatialSettings(state.spatial)) &&
    (method !== "setLiveSettings" || state.mode !== undefined && state.density !== undefined && state.composition !== undefined && state.spatial !== undefined) &&
    typeof state.error === "string" && state.error.length <= 4096 &&
    (state.status !== "listening" || state.enabled === true) && (state.status !== "off" || state.enabled === false);
}

function validPlaybackReply(method: string, value: unknown): boolean {
  if (!["getTransport", "setTransport", "prepareReplay", "getAudioState", "setAudioEnabled"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const v = value as Record<string, unknown>;
  if (method === "getTransport" || method === "setTransport") return (v.preparationId === null || identity(v.preparationId)) && unsignedDecimal(v.generation) &&
    ["playing", "paused", "stopped"].includes(v.state as string) && unsignedDecimal(v.positionUs) && unsignedDecimal(v.durationUs) && BigInt(v.positionUs) <= BigInt(v.durationUs);
  if (method === "prepareReplay") return identity(v.preparationId) && unsignedDecimal(v.generation) && unsignedDecimal(v.durationUs) && Number.isInteger(v.eventCount) && (v.eventCount as number) >= 0 && typeof v.peakEventsPerSecond === "number" && Number.isFinite(v.peakEventsPerSecond);
  return ["running", "disabled", "unavailable", "device_lost"].includes(v.status as string) && (v.deviceName === null || typeof v.deviceName === "string") &&
    typeof v.masterGainDb === "number" && Number.isFinite(v.masterGainDb) && (v.peakDbfs === null || (typeof v.peakDbfs === "number" && Number.isFinite(v.peakDbfs))) &&
    typeof v.meterIsSimulated === "boolean" && typeof v.clipping === "boolean" && (v.activeSynthVoices === null || (Number.isInteger(v.activeSynthVoices) && (v.activeSynthVoices as number) >= 0)) && Number.isInteger(v.maxSynthVoices) && (v.maxSynthVoices as number) > 0 &&
    (v.error === undefined || v.error === null || typeof v.error === "string");
}

function validCompositionReply(method: string, value: unknown): boolean {
  if (!["getComposition", "setComposition"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const result = value as Record<string, unknown>, settings = result.settings as Record<string, unknown>;
  const boundedInteger = (v: unknown, min: number, max: number) => Number.isInteger(v) && (v as number) >= min && (v as number) <= max;
  return !!settings && typeof settings === "object" && Object.keys(settings).length === 8 &&
    typeof settings.enabled === "boolean" && typeof settings.quantize === "boolean" &&
    boundedInteger(settings.tempoBpm, 40, 180) && boundedInteger(settings.tonicPitchClass, 0, 11) &&
    ["majorPentatonic", "minorPentatonic", "major", "naturalMinor"].includes(settings.scale as string) &&
    boundedInteger(settings.density, 0, 2) && boundedInteger(settings.seed, 0, 4294967295) &&
    typeof settings.variation === "number" && Number.isFinite(settings.variation) && settings.variation >= 0 && settings.variation <= 1 &&
    (result.preparation === null && method === "getComposition" || result.preparation !== null && validPlaybackReply("prepareReplay", result.preparation)) &&
    [result.generatedNotes, result.suppressed].every(v => v === undefined || boundedInteger(v, 0, 1000000)) &&
    (result.suppressedSourceEvents === undefined || boundedInteger(result.suppressedSourceEvents, 0, 50000));
}

function validSpatialReply(method: string, value: unknown): boolean {
  if (!["getSpatial", "setSpatial"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const result = value as Record<string, unknown>;
  return validSpatialSettings(result.settings) && (result.preparation === null && method === "getSpatial" ||
    result.preparation !== null && validPlaybackReply("prepareReplay", result.preparation)) &&
    [result.routedNotes, result.unassignedNotes].every(v => v === undefined || Number.isInteger(v) && (v as number) >= 0 && (v as number) <= 1000000) &&
    (result.wetActive === undefined || typeof result.wetActive === "boolean");
}

const opaqueId = (value: unknown): value is string => typeof value === "string" && /^[0-9a-f]{32}$/.test(value);
function validSceneSession(value: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const session = value as Record<string, unknown>;
  const timestamp = (v: unknown) => typeof v === "string" && /^-?(0|[1-9][0-9]{0,18})$/.test(v) && v !== "-0" && BigInt(v) >= -9223372036854775808n && BigInt(v) <= 9223372036854775807n;
  const bounds = (v: unknown) => v === undefined || v === null || Array.isArray(v) && v.length === 2 && v.every(timestamp) && BigInt(v[0]) <= BigInt(v[1]);
  return identity(session.id) && typeof session.name === "string" && session.name.length <= 512 &&
    ["frozen", "recording", "interrupted"].includes(session.state as string) && ["collection", "archive_snapshot", "imported_bundle"].includes(session.origin as string) &&
    Number.isInteger(session.eventCount) && (session.eventCount as number) >= 0 && typeof session.captureTruncated === "boolean" &&
    bounds(session.observedBoundsUs) && bounds(session.arrivalBoundsUs);
}
function validSoundscape(value: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const scene = value as Record<string, unknown>;
  return opaqueId(scene.id) && typeof scene.name === "string" && scene.name.length <= 512 && typeof scene.valid === "boolean" &&
    typeof scene.contentVerified === "boolean" && (scene.error === null || typeof scene.error === "string") &&
    (scene.contentHash === null || typeof scene.contentHash === "string" && /^[0-9a-f]{64}$/.test(scene.contentHash)) &&
    (scene.sourceSessionId === null || identity(scene.sourceSessionId)) && Number.isInteger(scene.eventCount) && (scene.eventCount as number) >= 0 && (scene.eventCount as number) <= 50000 &&
    (scene.durationUs === null || unsignedDecimal(scene.durationUs)) && [null, "observed", "arrival"].includes(scene.basis as null | string) &&
    (scene.speedNumerator === null || unsignedDecimal(scene.speedNumerator) && scene.speedNumerator !== "0") &&
    (scene.speedDenominator === null || unsignedDecimal(scene.speedDenominator) && scene.speedDenominator !== "0") &&
    (!scene.valid || scene.durationUs !== null && scene.basis !== null && scene.speedNumerator !== null && scene.speedDenominator !== null);
}
function validLiveSources(value: unknown, method: string, args: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const state = value as Record<string, unknown>;
  if (typeof state.collecting !== "boolean" || typeof state.libraryActive !== "boolean" ||
      state.libraryChanged !== undefined && typeof state.libraryChanged !== "boolean" ||
      !Array.isArray(state.sources) || ![2, 4].includes(state.sources.length)) return false;
  const ids = new Set();
  const timestamp = (v: unknown) => v === null || typeof v === "string" && v.length <= 40 && /^\d{4}-\d\d-\d\dT.*Z$/.test(v) && Number.isFinite(Date.parse(v));
  const valid = state.sources.every(source => {
    if (!source || !["birdweather", "nzsharks", "birdweather_world", "inaturalist"].includes(source.id) || ids.has(source.id) ||
        ![source.enabled, source.polling, source.complete, source.gap].every(v => typeof v === "boolean") ||
        ![source.lastAttemptUtc, source.lastSuccessUtc, source.lastObservationUtc].every(timestamp) ||
        !(source.error === null || typeof source.error === "string" && /^[a-z0-9_]{1,100}$/.test(source.error)) ||
        !unsignedDecimal(source.records) || !Number.isInteger(source.nextPollSeconds) || source.nextPollSeconds < 0 || source.nextPollSeconds > 86400) return false;
    if (source.regions !== undefined && (source.id !== "birdweather_world" || !Array.isArray(source.regions) || source.regions.length > 8 ||
        !source.regions.every((r: Record<string, unknown>) => r && typeof r.id === "string" && /^[a-z_]{1,32}$/.test(r.id) &&
          timestamp(r.lastAttemptUtc) && timestamp(r.lastObservationUtc) && typeof r.complete === "boolean" && typeof r.gap === "boolean" &&
          (r.error === null || typeof r.error === "string" && /^[a-z0-9_]{1,100}$/.test(r.error))))) return false;
    ids.add(source.id); return true;
  }) && ids.has("birdweather") && ids.has("nzsharks") &&
    state.sources.filter(s => s.enabled && (s.id === "birdweather" || s.id === "birdweather_world")).length <= 1 &&
    state.collecting === state.sources.some(source => source.enabled);
  if (state.sessionError !== undefined && state.sessionError !== null && (typeof state.sessionError !== "string" || state.sessionError.length > 4096)) return false;
  const session = state.session as Record<string, unknown> | null | undefined;
  if (session !== undefined && session !== null && (!state.collecting || !identity(session.id) || !identity(session.storeId) ||
      !unsignedDecimal(session.baselineRevision) || !timestamp(session.observedFromUtc) || session.observedFromUtc === null)) return false;
  if (session === null && state.collecting) return false;
  if (!valid || method !== "setLiveSources") return valid;
  const requested = (args as { sourceIds?: unknown } | null)?.sourceIds;
  return Array.isArray(requested) && requested.length <= 3 && new Set(requested).size === requested.length &&
    requested.every(id => ids.has(id)) &&
    state.sources.every(source => source.enabled === requested.includes(source.id));
}
function validSessionPage(value: unknown, args: unknown): boolean {
  if (!value || typeof value !== "object") return false;
  const page = value as Record<string, unknown>;
  const limit = (args as { limit?: unknown } | null)?.limit;
  if (!Number.isInteger(limit) || (limit as number) < 1 || (limit as number) > 100 ||
      !Array.isArray(page.items) || page.items.length > (limit as number) || !opaqueId(page.sourceId) || !unsignedDecimal(page.revision) ||
      !(page.nextCursor === null || typeof page.nextCursor === "string" && page.nextCursor.length > 0 && page.nextCursor.length <= 2048) ||
      page.items.length === 0 && page.nextCursor !== null) return false;
  const ids = new Set();
  return page.items.every(item => {
    if (!validSceneSession(item) || ids.has(item.id)) return false;
    ids.add(item.id); return true;
  });
}
function validSoundscapeReply(method: string, value: unknown, args: unknown): boolean {
  if (!["listExports", "listSoundscapes", "saveSoundscape", "recallSoundscape", "startSoundscapeRender", "getRenderJob", "cancelRender"].includes(method)) return true;
  if (!value || typeof value !== "object") return false;
  const result = value as Record<string, unknown>;
  if (method === "listExports") {
    if (!Array.isArray(result.items) || result.items.length > 100 || typeof result.limited !== "boolean" ||
        ![result.unavailableFiles, result.incompleteRenders].every(v => Number.isInteger(v) && (v as number) >= 0 && (v as number) <= 2048)) return false;
    const active = result.activeRender as Record<string, unknown> | null | undefined;
    if (active !== undefined && active !== null &&
        (!opaqueId(active.jobId) || !opaqueId(active.soundscapeId) || !["queued", "rendering"].includes(active.state as string) ||
         !validSoundscapeReply("getRenderJob", active, { jobId: active.jobId }))) return false;
    const ids = new Set<string>();
    const hash = (v: unknown) => typeof v === "string" && /^[0-9a-f]{64}$/.test(v);
    return result.items.every(item => {
      if (!item || !opaqueId(item.artifactId) || ids.has(item.artifactId) ||
          typeof item.sceneName !== "string" || item.sceneName.length > 512 || !hash(item.sceneHash) || !hash(item.engineSourceSha256) ||
          ![item.bytes, item.frames, item.durationUs].every(unsignedDecimal) ||
          !Number.isInteger(item.sampleRate) || item.sampleRate < 8000 || item.sampleRate > 192000 ||
          BigInt(item.bytes) < BigInt(item.frames) * 6n || BigInt(item.durationUs) !== BigInt(item.frames) * 1000000n / BigInt(item.sampleRate)) return false;
      ids.add(item.artifactId); return true;
    });
  }
  if (method === "saveSoundscape") return validSoundscape(value) && result.valid === true && result.contentVerified === true;
  if (method === "listSoundscapes") {
    if (!Array.isArray(result.items) || result.items.length > 100 || typeof result.limited !== "boolean" ||
        result.unmanagedFiles !== undefined && (!Number.isInteger(result.unmanagedFiles) || (result.unmanagedFiles as number) < 0 || (result.unmanagedFiles as number) > 100)) return false;
    const ids = new Set();
    return result.items.every(item => { if (!validSoundscape(item) || ids.has(item.id)) return false; ids.add(item.id); return true; });
  }
  if (method === "recallSoundscape") {
    const scene = result.soundscape as Record<string, unknown>, prep = result.preparation as Record<string, unknown>;
    const requested = (args as { soundscapeId: string }).soundscapeId;
    return validSoundscape(scene) && scene.id === requested && scene.valid === true && scene.contentVerified === true &&
      validPlaybackReply("prepareReplay", prep) && validSceneSession(prep.session) &&
      (prep.session as Record<string, unknown>).id === `scene:${requested}` && prep.eventCount === scene.eventCount && (prep.session as Record<string, unknown>).eventCount === scene.eventCount;
  }
  return identity(result.jobId) && ["queued", "rendering", "complete", "failed", "cancelled"].includes(result.state as string) &&
    typeof result.progress === "number" && Number.isFinite(result.progress) && result.progress >= 0 && result.progress <= 1 &&
    (result.message === null || typeof result.message === "string") &&
    (method === "startSoundscapeRender" ? opaqueId(result.jobId) && result.soundscapeId === (args as { soundscapeId: string }).soundscapeId : result.jobId === (args as { jobId: string }).jobId) &&
    (result.artifactId === undefined || result.artifactId === null || opaqueId(result.artifactId)) &&
    (result.filename === undefined || result.filename === null || typeof result.filename === "string" && result.filename.length <= 256 && !/[\\/]/.test(result.filename)) &&
    (result.soundscapeId === undefined || opaqueId(result.soundscapeId)) &&
    (result.state !== "complete" || result.soundscapeId === undefined || opaqueId(result.artifactId));
}

function validPieceSession(value: unknown): boolean {
  if (!validSceneSession(value)) return false;
  const session = value as Record<string, unknown>;
  return (session.id as string).length <= 128 && (session.eventCount as number) <= 50000 &&
    [session.metadataIntegrity, session.observedWindowPolicy].every(v => v === undefined || typeof v === "string" && v.length <= 128);
}
function validPieceReply(method: string, value: unknown, args: unknown): boolean {
  if (!["startPieceFreeze", "startPieceImport", "getPieceJob", "cancelPieceJob", "openManagedPiece"].includes(method)) return true;
  const request = args as { jobId?: string; sessionId?: string };
  if (value === null) return method === "getPieceJob";
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const reply = value as Record<string, unknown>;
  if (method === "openManagedPiece") return validPieceSession(reply.session) &&
    (reply.session as Record<string, unknown>).id === request.sessionId && typeof reply.libraryChanged === "boolean";
  if (!opaqueId(reply.id) || !["freeze", "import"].includes(reply.operation as string) ||
      !["running", "completed", "cancelled", "failed"].includes(reply.status as string) ||
      reply.stage !== undefined && (typeof reply.stage !== "string" || !/^[a-z_]{1,64}$/.test(reply.stage)) ||
      !Number.isInteger(reply.processed) || (reply.processed as number) < 0 || (reply.processed as number) > 50000 ||
      !(reply.total === null || Number.isInteger(reply.total) && (reply.total as number) >= (reply.processed as number) && (reply.total as number) <= 50000) ||
      !(reply.error === null || typeof reply.error === "string" && reply.error.length > 0 && reply.error.length <= 4096) ||
      (reply.status === "failed" ? reply.error === null : reply.error !== null) ||
      request.jobId !== undefined && reply.id !== request.jobId ||
      method === "startPieceFreeze" && reply.operation !== "freeze" || method === "startPieceImport" && reply.operation !== "import") return false;
  if (reply.status !== "completed") return reply.piece === null;
  return validPieceSession(reply.piece) && reply.total === reply.processed &&
    (reply.piece as Record<string, unknown>).eventCount === reply.processed &&
    (method !== "startPieceImport" || (reply.piece as Record<string, unknown>).id === request.sessionId);
}

let requestCounter = 0;
const nextRequestId = () => `ui-${Date.now().toString(36)}-${(requestCounter += 1)}`;

export class HostClient {
  readonly edits = new EditBarrier();
  private capabilities: HostCapabilities | null = null;
  private playbackMutation = 0;
  get playbackRevision() { return this.playbackMutation; }

  constructor(private readonly host: ExtendedHost | AtlasHost) {}

  get implementation(): HostCapabilities["implementation"] | "unknown" {
    return this.capabilities?.implementation ?? "unknown";
  }
  get caps(): HostCapabilities | null {
    return this.capabilities;
  }

  /** Fetch capabilities first; nothing else is offered until this succeeds. */
  async connect(signal?: AbortSignal): Promise<HostResult<HostCapabilities>> {
    const result = await this.send("getCapabilities", {} as Record<string, never>, { signal, skipCapabilityCheck: true });
    this.capabilities = null;
    if (result.ok) {
      const value = result.value;
      if (!value || value.protocolVersion !== 1 || !["native", "mock"].includes(value.implementation) ||
          !Array.isArray(value.supportedMethods) || value.supportedMethods.length > 100 ||
          !value.supportedMethods.every((method) => typeof method === "string") || typeof value.audioAvailable !== "boolean" ||
          value.supportsSmoothedLive !== undefined && typeof value.supportsSmoothedLive !== "boolean" ||
          value.libraryAvailable !== undefined && typeof value.libraryAvailable !== "boolean" ||
          !Number.isInteger(value.maxEventBatch) || value.maxEventBatch < 1 || value.maxEventBatch > 500 ||
          !Number.isInteger(value.maxVisibleMarkers) || value.maxVisibleMarkers < 1 || value.maxVisibleMarkers > 2000) {
        return { ok: false, error: { code: "unsupported", message: "Invalid or unsupported native capabilities" } };
      }
      this.capabilities = value;
    }
    return result;
  }

  supports(method: MethodName): boolean {
    return (this.capabilities?.supportedMethods as string[] | undefined)?.includes(method) ?? false;
  }

  subscribe(listener: (notification: HostNotification) => void): () => void {
    const host = this.host as ExtendedHost;
    return host.subscribe ? host.subscribe(listener) : () => undefined;
  }

  call<M extends MethodName>(method: M, args: AllMethods[M]["request"], options: { signal?: AbortSignal; timeoutMs?: number } = {}) {
    if (["prepareReplay", "setTransport", "setAudioEnabled", "setLiveEnabled", "recallSoundscape", "setComposition", "setSpatial", "setAudioDevice", "openManagedPiece"].includes(method)) this.playbackMutation += 1;
    const request = this.send(method, args, options);
    return ["setVoiceParameter", "setVoiceType", "setMasterGain", "saveVoice", "revertVoice", "applyVoicePreset", "setComposition", "setSpatial"].includes(method)
      ? this.edits.track(request) : request;
  }

  private async send<M extends MethodName>(
    method: M,
    args: AllMethods[M]["request"],
    options: { signal?: AbortSignal; timeoutMs?: number; skipCapabilityCheck?: boolean },
  ): Promise<HostResult<AllMethods[M]["response"]>> {
    if (!options.skipCapabilityCheck && !this.supports(method)) {
      return { ok: false, error: { code: "unsupported", message: `${method} is not offered by this host` } };
    }
    if (method === "setLiveSettings" && (args as Partial<import("./contracts").LiveSettings>).mode === "smoothed" && this.capabilities?.supportsSmoothedLive !== true) {
      return { ok: false, error: { code: "unsupported", message: "Smoothed live mode is not offered by this host" } };
    }
    if (options.signal?.aborted) return { ok: false, error: { code: "aborted", message: "Aborted before sending" } };
    const requestId = nextRequestId();
    const timeoutMs = options.timeoutMs ?? 8000;
    const controller = new AbortController();
    const forwardAbort = () => controller.abort();
    options.signal?.addEventListener("abort", forwardAbort, { once: true });
    let timedOut = false;
    let onAbort: () => void = () => undefined;
    const interrupted = new Promise<HostResult<never>>((resolve) => {
      onAbort = () => resolve({ ok: false, error: {
        code: timedOut ? "timeout" : "aborted",
        message: timedOut ? `${method} did not answer within ${timeoutMs} ms` : "Request cancelled",
      } });
      controller.signal.addEventListener("abort", onAbort, { once: true });
      if (controller.signal.aborted) onAbort();
    });
    const timer = window.setTimeout(() => { timedOut = true; controller.abort(); }, timeoutMs);
    try {
      const request = { protocolVersion: 1 as const, requestId, method, args };
      const reply = (this.host as ExtendedHost).request(request, controller.signal).then((r): HostResult<AllMethods[M]["response"]> => {
        if (!r || typeof r !== "object" || typeof r.ok !== "boolean" || r.requestId !== requestId) return { ok: false, error: { code: "internal", message: "Reply did not match request id" } };
        if (r.ok) return "result" in r && validBaselineTotal(method, r.result) && validLiveActivityReply(method, r.result, args) && validLiveSessionReply(method, r.result, args) && validLiveListeningReply(method, r.result) && validPlaybackReply(method, r.result) && validVoiceReply(method, r.result, args) && validSoundscapeReply(method, r.result, args) && validCompositionReply(method, r.result) && validSpatialReply(method, r.result) && validPieceReply(method, r.result, args) && (method !== "listSessionsPage" || validSessionPage(r.result, args)) && (!["getLiveSources", "setLiveSources"].includes(method) || validLiveSources(r.result, method, args)) && (!["getAudioDevices", "setAudioDevice"].includes(method) || validAudioDeviceCatalog(r.result)) ? { ok: true, value: r.result } : { ok: false, error: { code: "internal", message: "Malformed host result" } };
        if (!r.error || typeof r.error.message !== "string" || !["unsupported", "invalid_request", "unavailable", "stale_generation", "internal"].includes(r.error.code))
          return { ok: false, error: { code: "internal", message: "Malformed host error" } };
        return { ok: false, error: r.error };
      });
      const result = await Promise.race([reply, interrupted]);
      if (method === "setLiveSettings" && (args as Partial<import("./contracts").LiveSettings>).mode === "smoothed" && this.capabilities?.supportsSmoothedLive !== true) {
      return { ok: false, error: { code: "unsupported", message: "Smoothed live mode is not offered by this host" } };
    }
    if (options.signal?.aborted) return { ok: false, error: { code: "aborted", message: "Superseded by a newer request" } };
      return result;
    } catch (error) {
      return { ok: false, error: { code: "internal", message: error instanceof Error ? error.message : String(error) } };
    } finally {
      controller.signal.removeEventListener("abort", onAbort);
      window.clearTimeout(timer);
      options.signal?.removeEventListener("abort", forwardAbort);
    }
  }
}
