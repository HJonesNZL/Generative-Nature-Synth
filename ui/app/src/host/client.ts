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
    if (["prepareReplay", "setTransport", "setAudioEnabled", "recallSoundscape", "setComposition", "setSpatial", "setAudioDevice"].includes(method)) this.playbackMutation += 1;
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
        if (r.ok) return "result" in r && validPlaybackReply(method, r.result) && validVoiceReply(method, r.result, args) && validSoundscapeReply(method, r.result, args) && validCompositionReply(method, r.result) && validSpatialReply(method, r.result) && (!["getAudioDevices", "setAudioDevice"].includes(method) || validAudioDeviceCatalog(r.result)) ? { ok: true, value: r.result } : { ok: false, error: { code: "internal", message: "Malformed host result" } };
        if (!r.error || typeof r.error.message !== "string" || !["unsupported", "invalid_request", "unavailable", "stale_generation", "internal"].includes(r.error.code))
          return { ok: false, error: { code: "internal", message: "Malformed host error" } };
        return { ok: false, error: r.error };
      });
      const result = await Promise.race([reply, interrupted]);
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
